#include "common/event/dispatcher_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/event/file_event_impl.h"
#include "common/event/libevent_scheduler.h"
#include "common/event/signal_impl.h"
#include "common/event/timer_impl.h"
#include "common/filesystem/watcher_impl.h"
#include "common/network/buffer_source_socket.h"
#include "common/network/connection_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/listener_impl.h"
#include "common/network/pipe_connection_impl.h"
#include "common/network/udp_listener_impl.h"

#include "event2/event.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif

namespace Envoy {
namespace Event {

DispatcherImpl::DispatcherImpl(const std::string& name, Api::Api& api,
                               Event::TimeSystem& time_system)
    : DispatcherImpl(name, std::make_unique<Buffer::WatermarkBufferFactory>(), api, time_system) {}

DispatcherImpl::DispatcherImpl(const std::string& name, Buffer::WatermarkFactoryPtr&& factory,
                               Api::Api& api, Event::TimeSystem& time_system)
    : name_(name), api_(api), buffer_factory_(std::move(factory)),
      scheduler_(time_system.createScheduler(base_scheduler_, base_scheduler_)),
      deferred_delete_cb_(base_scheduler_.createSchedulableCallback(
          [this]() -> void { clearDeferredDeleteList(); })),
      post_cb_(base_scheduler_.createSchedulableCallback([this]() -> void { runPostCallbacks(); })),
      current_to_delete_(&to_delete_1_) {
  ASSERT(!name_.empty());
#ifdef ENVOY_HANDLE_SIGNALS
  SignalAction::registerFatalErrorHandler(*this);
#endif
  updateApproximateMonotonicTimeInternal();
  base_scheduler_.registerOnPrepareCallback([this]() {
    ENVOY_LOG_MISC(debug, "lambdai: libevent loop................................................");
    this->updateApproximateMonotonicTime();
  });
}

DispatcherImpl::~DispatcherImpl() {
#ifdef ENVOY_HANDLE_SIGNALS
  SignalAction::removeFatalErrorHandler(*this);
#endif
}

void DispatcherImpl::initializeStats(Stats::Scope& scope,
                                     const absl::optional<std::string>& prefix) {
  const std::string effective_prefix = prefix.has_value() ? *prefix : absl::StrCat(name_, ".");
  // This needs to be run in the dispatcher's thread, so that we have a thread id to log.
  post([this, &scope, effective_prefix] {
    stats_prefix_ = effective_prefix + "dispatcher";
    stats_ = std::make_unique<DispatcherStats>(
        DispatcherStats{ALL_DISPATCHER_STATS(POOL_HISTOGRAM_PREFIX(scope, stats_prefix_ + "."))});
    base_scheduler_.initializeStats(stats_.get());
    ENVOY_LOG(debug, "running {} on thread {}", stats_prefix_, run_tid_.debugString());
  });
}

void DispatcherImpl::clearDeferredDeleteList() {
  ASSERT(isThreadSafe());
  std::vector<DeferredDeletablePtr>* to_delete = current_to_delete_;

  size_t num_to_delete = to_delete->size();
  if (deferred_deleting_ || !num_to_delete) {
    return;
  }

  ENVOY_LOG(trace, "clearing deferred deletion list (size={})", num_to_delete);

  // Swap the current deletion vector so that if we do deferred delete while we are deleting, we
  // use the other vector. We will get another callback to delete that vector.
  if (current_to_delete_ == &to_delete_1_) {
    current_to_delete_ = &to_delete_2_;
  } else {
    current_to_delete_ = &to_delete_1_;
  }

  deferred_deleting_ = true;

  // Calling clear() on the vector does not specify which order destructors run in. We want to
  // destroy in FIFO order so just do it manually. This required 2 passes over the vector which is
  // not optimal but can be cleaned up later if needed.
  for (size_t i = 0; i < num_to_delete; i++) {
    (*to_delete)[i].reset();
  }

  to_delete->clear();
  deferred_deleting_ = false;
}

Network::ConnectionPtr
DispatcherImpl::createServerConnection(Network::ConnectionSocketPtr&& socket,
                                       Network::TransportSocketPtr&& transport_socket,
                                       StreamInfo::StreamInfo& stream_info) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::ConnectionImpl>(*this, std::move(socket),
                                                   std::move(transport_socket), stream_info, true);
}

Network::ClientConnectionPtr
DispatcherImpl::createClientConnection(Network::Address::InstanceConstSharedPtr address,
                                       Network::Address::InstanceConstSharedPtr source_address,
                                       Network::TransportSocketPtr&& transport_socket,
                                       const Network::ConnectionSocket::OptionsSharedPtr& options) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::ClientConnectionImpl>(*this, address, source_address,
                                                         std::move(transport_socket), options);
}

Network::ClientConnectionPtr
DispatcherImpl::createUserspacePipe(Network::Address::InstanceConstSharedPtr peer_address,
                                    Network::Address::InstanceConstSharedPtr local_address) {
  ASSERT(isThreadSafe());
  if (peer_address == nullptr) {
    return nullptr;
  }
  // Find the listener callback. The listener is supposed to setup the server connection.
  auto iter = pipe_listeners_.find(peer_address->asString());
  for (const auto& [name, _] : pipe_listeners_) {
    ENVOY_LOG_MISC(debug, "lambdai: p listener {}", name);
  }
  if (iter == pipe_listeners_.end()) {
    ENVOY_LOG_MISC(debug, "lambdai: no valid listener registered for envoy internal address {}",
                   peer_address->asString());
    return nullptr;
  }
  auto client_socket = std::make_unique<Network::BufferSourceSocket>();
  auto server_socket = std::make_unique<Network::BufferSourceSocket>();
  auto client_socket_raw = client_socket.get();
  auto server_socket_raw = server_socket.get();
  auto client_conn = std::make_unique<Network::ClientPipeImpl>(
      *this, peer_address, local_address, std::move(client_socket), *client_socket_raw, nullptr);
  ENVOY_LOG_MISC(debug, "lambdai: client pipe C{} owns TS{} and B{}", client_conn->id(),
                 client_socket_raw->bsid(), client_socket_raw->read_buffer_.bid());

  auto server_conn = std::make_unique<Network::ServerPipeImpl>(
      *this, local_address, peer_address, std::move(server_socket), *server_socket_raw, nullptr);
  ENVOY_LOG_MISC(debug, "lambdai: server pipe C{} owns TS{} and B{}", server_conn->id(),
                 server_socket_raw->bsid(), server_socket_raw->read_buffer_.bid());

  server_conn->setPeer(client_conn.get());
  client_conn->setPeer(server_conn.get());
  // TODO(lambdai): Retrieve buffer each time when supporting close.
  // TODO(lambdai): Add to dest buffer to generic IoHandle, or TransportSocketCallback.
  // client_socket_raw->setReadSourceBuffer(&server_conn->getWriteBuffer().buffer);
  client_socket_raw->setWritablePeer(server_socket_raw);
  client_socket_raw->setEventSchedulable(client_conn.get());
  // server_socket_raw->setReadSourceBuffer(&client_conn->getWriteBuffer().buffer);
  server_socket_raw->setWritablePeer(client_socket_raw);
  server_socket_raw->setEventSchedulable(server_conn.get());
  (iter->second)(peer_address, std::move(server_conn));
  return client_conn;
}

void DispatcherImpl::registerPipeFactory(const std::string& pipe_listener_id,
                                         DispatcherImpl::PipeFactory pipe_factory) {
  ENVOY_LOG_MISC(debug, "lambdai: register pipe factory on address {}", pipe_listener_id);
  pipe_listeners_[pipe_listener_id] = pipe_factory;
}

Network::DnsResolverSharedPtr DispatcherImpl::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers,
    const bool use_tcp_for_dns_lookups) {
  ASSERT(isThreadSafe());
  return Network::DnsResolverSharedPtr{
      new Network::DnsResolverImpl(*this, resolvers, use_tcp_for_dns_lookups)};
}

FileEventPtr DispatcherImpl::createFileEvent(os_fd_t fd, FileReadyCb cb, FileTriggerType trigger,
                                             uint32_t events) {
  ASSERT(isThreadSafe());
  return FileEventPtr{new FileEventImpl(*this, fd, cb, trigger, events)};
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  ASSERT(isThreadSafe());
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this, api_)};
}

Network::ListenerPtr DispatcherImpl::createListener(Network::SocketSharedPtr&& socket,
                                                    Network::ListenerCallbacks& cb,
                                                    bool bind_to_port, const std::string& name) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::ListenerImpl>(*this, std::move(socket), cb, bind_to_port, name);
}

Network::UdpListenerPtr DispatcherImpl::createUdpListener(Network::SocketSharedPtr&& socket,
                                                          Network::UdpListenerCallbacks& cb) {
  ASSERT(isThreadSafe());
  return std::make_unique<Network::UdpListenerImpl>(*this, std::move(socket), cb, timeSource());
}

TimerPtr DispatcherImpl::createTimer(TimerCb cb) {
  ASSERT(isThreadSafe());
  return createTimerInternal(cb);
}

Event::SchedulableCallbackPtr DispatcherImpl::createSchedulableCallback(std::function<void()> cb) {
  ASSERT(isThreadSafe());
  return base_scheduler_.createSchedulableCallback(cb);
}

TimerPtr DispatcherImpl::createTimerInternal(TimerCb cb) {
  return scheduler_->createTimer(cb, *this);
}

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  ASSERT(isThreadSafe());
  current_to_delete_->emplace_back(std::move(to_delete));
  ENVOY_LOG(trace, "item added to deferred deletion list (size={})", current_to_delete_->size());
  if (1 == current_to_delete_->size()) {
    deferred_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::exit() { base_scheduler_.loopExit(); }

SignalEventPtr DispatcherImpl::listenForSignal(int signal_num, SignalCb cb) {
  ASSERT(isThreadSafe());
  return SignalEventPtr{new SignalEventImpl(*this, signal_num, cb)};
}

void DispatcherImpl::post(std::function<void()> callback) {
  bool do_post;
  {
    Thread::LockGuard lock(post_lock_);
    do_post = post_callbacks_.empty();
    post_callbacks_.push_back(callback);
  }

  if (do_post) {
    post_cb_->scheduleCallbackCurrentIteration();
  }
}

void DispatcherImpl::run(RunType type) {
  run_tid_ = api_.threadFactory().currentThreadId();

  // Flush all post callbacks before we run the event loop. We do this because there are post
  // callbacks that have to get run before the initial event loop starts running. libevent does
  // not guarantee that events are run in any particular order. So even if we post() and call
  // event_base_once() before some other event, the other event might get called first.
  runPostCallbacks();
  base_scheduler_.run(type);
}

MonotonicTime DispatcherImpl::approximateMonotonicTime() const {
  return approximate_monotonic_time_;
}

void DispatcherImpl::updateApproximateMonotonicTime() { updateApproximateMonotonicTimeInternal(); }

void DispatcherImpl::updateApproximateMonotonicTimeInternal() {
  approximate_monotonic_time_ = api_.timeSource().monotonicTime();
}

void DispatcherImpl::runPostCallbacks() {
  while (true) {
    // It is important that this declaration is inside the body of the loop so that the callback is
    // destructed while post_lock_ is not held. If callback is declared outside the loop and reused
    // for each iteration, the previous iteration's callback is destructed when callback is
    // re-assigned, which happens while holding the lock. This can lead to a deadlock (via
    // recursive mutex acquisition) if destroying the callback runs a destructor, which through some
    // callstack calls post() on this dispatcher.
    std::function<void()> callback;
    {
      Thread::LockGuard lock(post_lock_);
      if (post_callbacks_.empty()) {
        return;
      }
      callback = post_callbacks_.front();
      post_callbacks_.pop_front();
    }
    callback();
  }
}

} // namespace Event
} // namespace Envoy
