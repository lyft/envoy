#include "common/signal/fatal_error_handler.h"

#include <atomic>
#include <list>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/signal/fatal_action.h"

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace FatalErrorHandler {

namespace {

ABSL_CONST_INIT static absl::Mutex failure_mutex(absl::kConstInit);
// Since we can't grab the failure mutex on fatal error (snagging locks under
// fatal crash causing potential deadlocks) access the handler list as an atomic
// operation, which is async-signal-safe. If the crash handler runs at the same
// time as another thread tries to modify the list, one of them will get the
// list and the other will get nullptr instead. If the crash handler loses the
// race and gets nullptr, it won't run any of the registered error handlers.
using FailureFunctionList = std::list<const FatalErrorHandlerInterface*>;
ABSL_CONST_INIT std::atomic<FailureFunctionList*> fatal_error_handlers{nullptr};

// Use an atomic operation since on fatal error we'll consume the
// fatal_action_manager and don't want to have any locks as they aren't
// async-signal-safe.
ABSL_CONST_INIT std::atomic<FatalAction::FatalActionManager*> fatal_action_manager{nullptr};
ABSL_CONST_INIT std::atomic<int64_t> failure_tid{-1};

// Helper function to run fatal actions.
void runFatalActions(const FatalAction::FatalActionPtrList& actions) {
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    return;
  }

  // Get the dispatcher and its tracked object.
  for (auto* handler : *list) {
    handler->runFatalActionsOnTrackedObject(actions);
  }

  fatal_error_handlers.store(list, std::memory_order_release);
}
} // namespace

void registerFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    list = new FailureFunctionList;
  }
  list->push_back(&handler);
  fatal_error_handlers.store(list, std::memory_order_release);
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void removeFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    // removeFatalErrorHandler() may see an empty list of fatal error handlers
    // if it's called at the same time as callFatalErrorHandlers(). In that case
    // Envoy is in the middle of crashing anyway, but don't add a segfault on
    // top of the crash.
    return;
  }
  list->remove(&handler);
  if (list->empty()) {
    delete list;
  } else {
    fatal_error_handlers.store(list, std::memory_order_release);
  }
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void callFatalErrorHandlers(std::ostream& os) {
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list != nullptr) {
    for (const auto* handler : *list) {
      handler->onFatalError(os);
    }
    fatal_error_handlers.store(list, std::memory_order_release);
  }
}

void registerFatalActions(FatalAction::FatalActionPtrList safe_actions,
                          FatalAction::FatalActionPtrList unsafe_actions,
                          Thread::ThreadFactory& thread_factory) {
  // Create a FatalActionManager and try to store it. If we fail to store
  // our manager, it'll be deleted due to the unique_ptr.
  auto mananger = std::make_unique<FatalAction::FatalActionManager>(
      std::move(safe_actions), std::move(unsafe_actions), thread_factory);
  FatalAction::FatalActionManager* unset_manager = nullptr;

  if (fatal_action_manager.compare_exchange_strong(unset_manager, mananger.get(),
                                                   std::memory_order_acq_rel)) {
    // Our manager is the system's singleton, ensure that the unique_ptr does not
    // delete the instance.
    mananger.release();
  } else {
    ENVOY_BUG(false, "Fatal Actions have already been registered.");
  }
}

FatalAction::Status runSafeActions() {
  // Check that registerFatalActions has already been called.
  FatalAction::FatalActionManager* action_manager =
      fatal_action_manager.load(std::memory_order_acquire);

  if (action_manager == nullptr) {
    return FatalAction::Status::ActionManangerUnset;
  }

  // Check that we're the thread that gets to run the actions.
  int64_t my_tid = action_manager->getThreadFactory().currentThreadId().getId();
  int64_t expected_tid = -1;

  if (failure_tid.compare_exchange_strong(expected_tid, my_tid, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
    // Run the actions
    runFatalActions(action_manager->getSafeActions());
    return FatalAction::Status::Success;
  } else if (expected_tid == my_tid) {
    return FatalAction::Status::AlreadyRanOnThisThread;
  }

  return FatalAction::Status::RunningOnAnotherThread;
}

FatalAction::Status runUnsafeActions() {
  // Check that registerFatalActions has already been called.
  FatalAction::FatalActionManager* action_manager =
      fatal_action_manager.load(std::memory_order_acquire);

  if (action_manager == nullptr) {
    return FatalAction::Status::ActionManangerUnset;
  }

  // Check that we're the thread that gets to run the actions.
  int64_t my_tid = action_manager->getThreadFactory().currentThreadId().getId();
  int64_t failing_tid = failure_tid.load(std::memory_order_acquire);

  if (my_tid == failing_tid) {
    // Run the actions
    runFatalActions(action_manager->getUnsafeActions());
    return FatalAction::Status::Success;
  } else if (failing_tid == -1) {
    return FatalAction::Status::SafeActionsNotYetRan;
  }
  return FatalAction::Status::RunningOnAnotherThread;
}

void clearFatalActionsOnTerminate() {
  auto* raw_ptr = fatal_action_manager.exchange(nullptr, std::memory_order_relaxed);
  if (raw_ptr != nullptr) {
    delete raw_ptr;
  }
}

// This resets the internal state of Fatal Action for the module.
// This is necessary as it allows us to have multiple test cases invoke the
// fatal actions without state from other tests leaking in.
void resetFatalActionStateForTest() {
  // Free the memory of the Fatal Action, since it's not managed by a smart
  // pointer. This prevents memory leaks in tests.
  auto* raw_ptr = fatal_action_manager.exchange(nullptr, std::memory_order_relaxed);
  if (raw_ptr != nullptr) {
    delete raw_ptr;
  }
  failure_tid.store(-1, std::memory_order_release);
}

} // namespace FatalErrorHandler
} // namespace Envoy
