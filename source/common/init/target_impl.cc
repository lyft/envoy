#include "common/init/target_impl.h"

#include "absl/memory/memory.h"

namespace Envoy {
namespace Init {

TargetHandleImpl::TargetHandleImpl(absl::string_view handle_name, absl::string_view name,
                                   std::weak_ptr<InternalInitalizeFn> fn)
    : handle_name_(handle_name), name_(name), fn_(std::move(fn)) {}

bool TargetHandleImpl::initialize(const Watcher& watcher) const {
  auto locked_fn(fn_.lock());
  if (locked_fn) {
    // If we can "lock" a shared pointer to the target's callback function, call it
    // with a new handle to the ManagerImpl's watcher that was passed in.
    ENVOY_LOG(debug, "{} initializing {}", handle_name_, name_);
    (*locked_fn)(watcher.createHandle(name_));
    return true;
  } else {
    // If not, the target was already destroyed.
    ENVOY_LOG(debug, "{} can't initialize {} (unavailable)", handle_name_, name_);
    return false;
  }
}

TargetImpl::TargetImpl(absl::string_view name, InitializeFn fn)
    : name_(fmt::format("target {}", name)),
      fn_(std::make_shared<InternalInitalizeFn>([this, fn](WatcherHandlePtr watcher_handle) {
        watcher_handle_ = std::move(watcher_handle);
        fn();
      })) {}

TargetImpl::~TargetImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view TargetImpl::name() const { return name_; }

TargetHandlePtr TargetImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique here because TargetHandleImpl ctor is private.
  return absl::WrapUnique(
      new TargetHandleImpl(handle_name, name_, std::weak_ptr<InternalInitalizeFn>(fn_)));
}

bool TargetImpl::ready() {
  if (watcher_handle_) {
    // If we have a handle for the ManagerImpl's watcher, signal it and then reset so it can't be
    // accidentally signaled again.
    const bool result = watcher_handle_->ready();
    watcher_handle_.reset();
    return result;
  }
  return false;
}

SharedTargetImpl::SharedTargetImpl(absl::string_view name, InitializeFn fn)
    : name_(fmt::format("shared target {}", name)),
      fn_(std::make_shared<InternalInitalizeFn>([this, fn](WatcherHandlePtr watcher_handle) {
        if (is_initialization_done_) {
          watcher_handle->ready();
        } else {
          watcher_handles_.push_back(std::move(watcher_handle));
          std::call_once(once_flag_, fn);
        }
      })) {}

SharedTargetImpl::~SharedTargetImpl() { ENVOY_LOG(debug, "{} destroyed", name_); }

absl::string_view SharedTargetImpl::name() const { return name_; }

TargetHandlePtr SharedTargetImpl::createHandle(absl::string_view handle_name) const {
  // Note: can't use std::make_unique here because TargetHandleImpl ctor is private.
  return absl::WrapUnique(
      new TargetHandleImpl(handle_name, name_, std::weak_ptr<InternalInitalizeFn>(fn_)));
}

bool SharedTargetImpl::ready() {
  is_initialization_done_ = true;
  for (auto& watcher_handle : watcher_handles_) {
    watcher_handle->ready();
  }
  // save heap and avoid repeatedly invoke
  watcher_handles_.clear();
  return true;
}

} // namespace Init
} // namespace Envoy
