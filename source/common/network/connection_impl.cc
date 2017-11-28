#include "common/network/connection_impl.h"

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/common/exception.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

namespace {
Address::InstanceConstSharedPtr getNullLocalAddress(const Address::Instance& address) {
  if (address.type() == Address::Type::Ip && address.ip()->version() == Address::IpVersion::v6) {
    return Utility::getIpv6AnyAddress();
  }
  // Default to IPv4 any address.
  return Utility::getIpv4AnyAddress();
}
} // namespace

void ConnectionImplUtility::updateBufferStats(uint64_t delta, uint64_t new_total,
                                              uint64_t& previous_total, Stats::Counter& stat_total,
                                              Stats::Gauge& stat_current) {
  if (delta) {
    stat_total.add(delta);
  }

  if (new_total != previous_total) {
    if (new_total > previous_total) {
      stat_current.add(new_total - previous_total);
    } else {
      stat_current.sub(previous_total - new_total);
    }

    previous_total = new_total;
  }
}

std::atomic<uint64_t> ConnectionImpl::next_global_id_;

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                               Address::InstanceConstSharedPtr remote_address,
                               Address::InstanceConstSharedPtr local_address,
                               Address::InstanceConstSharedPtr bind_to_address,
                               bool using_original_dst, bool connected)
    : ConnectionImpl(dispatcher, fd, remote_address, local_address, bind_to_address,
                     TransportSocketPtr{new RawBufferSocket}, using_original_dst, connected) {}

ConnectionImpl::ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                               Address::InstanceConstSharedPtr remote_address,
                               Address::InstanceConstSharedPtr local_address,
                               Address::InstanceConstSharedPtr bind_to_address,
                               TransportSocketPtr transport_socket, bool using_original_dst,
                               bool connected)
    : filter_manager_(*this, *this), remote_address_(remote_address),
      local_address_((local_address == nullptr) ? getNullLocalAddress(*remote_address)
                                                : local_address),

      write_buffer_(
          dispatcher.getWatermarkFactory().create([this]() -> void { this->onLowWatermark(); },
                                                  [this]() -> void { this->onHighWatermark(); })),
      transport_socket_(std::move(transport_socket)), dispatcher_(dispatcher), fd_(fd),
      id_(++next_global_id_), using_original_dst_(using_original_dst) {

  // Treat the lack of a valid fd (which in practice only happens if we run out of FDs) as an OOM
  // condition and just crash.
  RELEASE_ASSERT(fd_ != -1);

  if (!connected) {
    state_ |= InternalState::Connecting;
  }

  // We never ask for both early close and read at the same time. If we are reading, we want to
  // consume all available data.
  file_event_ = dispatcher_.createFileEvent(
      fd_, [this](uint32_t events) -> void { onFileEvent(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  if (bind_to_address != nullptr) {
    int rc = bind_to_address->bind(fd);
    if (rc < 0) {
      ENVOY_LOG_MISC(debug, "Bind failure. Failed to bind to {}: {}", bind_to_address->asString(),
                     strerror(errno));
      // Set a special error state to ensure asynchronous close to give the owner of the
      // ConnectionImpl a chance to add callbacks and detect the "disconnect"
      state_ |= InternalState::BindError;

      // Trigger a write event to close this connection out-of-band.
      file_event_->activate(Event::FileReadyType::Write);
    }
  }

  transport_socket_->setTransportSocketCallbacks(*this);
}

ConnectionImpl::~ConnectionImpl() {
  ASSERT(fd_ == -1);

  // In general we assume that owning code has called close() previously to the destructor being
  // run. This generally must be done so that callbacks run in the correct context (vs. deferred
  // deletion). Hence the assert above. However, call close() here just to be completely sure that
  // the fd is closed and make it more likely that we crash from a bad close callback.
  close(ConnectionCloseType::NoFlush);
}

void ConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ConnectionImpl::addFilter(FilterSharedPtr filter) { filter_manager_.addFilter(filter); }

void ConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

bool ConnectionImpl::initializeReadFilters() { return filter_manager_.initializeReadFilters(); }

void ConnectionImpl::close(ConnectionCloseType type) {
  if (fd_ == -1) {
    return;
  }

  uint64_t data_to_write = write_buffer_->length();
  ENVOY_CONN_LOG(debug, "closing data_to_write={} type={}", *this, data_to_write, enumToInt(type));
  if (data_to_write == 0 || type == ConnectionCloseType::NoFlush ||
      !transport_socket_->canFlushClose()) {
    if (data_to_write > 0) {
      // We aren't going to wait to flush, but try to write as much as we can if there is pending
      // data.
      transport_socket_->doWrite(*write_buffer_);
    }

    closeSocket(ConnectionEvent::LocalClose);
  } else {
    // TODO(mattklein123): We need a flush timer here. We might never get open socket window.
    ASSERT(type == ConnectionCloseType::FlushWrite);
    state_ |= InternalState::CloseWithFlush;
    state_ &= ~InternalState::ReadEnabled;
    file_event_->setEnabled(Event::FileReadyType::Write | Event::FileReadyType::Closed);
  }
}

Connection::State ConnectionImpl::state() const {
  if (fd_ == -1) {
    return State::Closed;
  } else if (state_ & InternalState::CloseWithFlush) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ConnectionImpl::closeSocket(ConnectionEvent close_type) {
  if (fd_ == -1) {
    return;
  }

  ENVOY_CONN_LOG(debug, "closing socket: {}", *this, static_cast<uint32_t>(close_type));
  transport_socket_->closeSocket(close_type);

  // Drain input and output buffers.
  updateReadBufferStats(0, 0);
  updateWriteBufferStats(0, 0);
  connection_stats_.reset();

  file_event_.reset();
  ::close(fd_);
  fd_ = -1;

  raiseEvent(close_type);
}

Event::Dispatcher& ConnectionImpl::dispatcher() { return dispatcher_; }

void ConnectionImpl::noDelay(bool enable) {
  // There are cases where a connection to localhost can immediately fail (e.g., if the other end
  // does not have enough fds, reaches a backlog limit, etc.). Because we run with deferred error
  // events, the calling code may not yet know that the connection has failed. This is one call
  // where we go outside of libevent and hit the fd directly and this case can fail if the fd is
  // invalid. For this call instead of plumbing through logic that will immediately indicate that a
  // connect failed, we will just ignore the noDelay() call if the socket is invalid since error is
  // going to be raised shortly anyway and it makes the calling code simpler.
  if (fd_ == -1) {
    return;
  }

  // Don't set NODELAY for unix domain sockets
  sockaddr addr;
  socklen_t len = sizeof(addr);
  int rc = getsockname(fd_, &addr, &len);
  RELEASE_ASSERT(rc == 0);

  if (addr.sa_family == AF_UNIX) {
    return;
  }

  // Set NODELAY
  int new_value = enable;
  rc = setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &new_value, sizeof(new_value));
#ifdef __APPLE__
  if (-1 == rc && errno == EINVAL) {
    // Sometimes occurs when the connection is not yet fully formed. Empirically, TCP_NODELAY is
    // enabled despite this result.
    return;
  }
#endif

  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);
}

uint64_t ConnectionImpl::id() const { return id_; }

void ConnectionImpl::onRead(uint64_t read_buffer_size) {
  if (!(state_ & InternalState::ReadEnabled)) {
    return;
  }

  if (read_buffer_size == 0) {
    return;
  }

  filter_manager_.onRead();
}

void ConnectionImpl::readDisable(bool disable) {
  ASSERT(state() == State::Open);

  bool read_enabled = readEnabled();
  UNREFERENCED_PARAMETER(read_enabled);
  ENVOY_CONN_LOG(trace, "readDisable: enabled={} disable={}", *this, read_enabled, disable);

  // When we disable reads, we still allow for early close notifications (the equivalent of
  // EPOLLRDHUP for an epoll backend). For backends that support it, this allows us to apply
  // back pressure at the kernel layer, but still get timely notification of a FIN. Note that
  // we are not gaurenteed to get notified, so even if the remote has closed, we may not know
  // until we try to write. Further note that currently we don't correctly handle half closed
  // TCP connections in the sense that we assume that a remote FIN means the remote intends a
  // full close.
  //
  // TODO(mattklein123): Potentially support half-closed TCP connections. It's unclear if this is
  // required for any scenarios in which Envoy will be used (I don't know of any).
  if (disable) {
    if (!read_enabled) {
      ++read_disable_count_;
      return;
    }
    ASSERT(read_enabled);
    state_ &= ~InternalState::ReadEnabled;
    if (detect_early_close_) {
      file_event_->setEnabled(Event::FileReadyType::Write | Event::FileReadyType::Closed);
    } else {
      file_event_->setEnabled(Event::FileReadyType::Write);
    }
  } else {
    if (read_disable_count_ > 0) {
      --read_disable_count_;
      return;
    }
    ASSERT(!read_enabled);
    state_ |= InternalState::ReadEnabled;
    // We never ask for both early close and read at the same time. If we are reading, we want to
    // consume all available data.
    file_event_->setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write);
    // If the connection has data buffered there's no guarantee there's also data in the kernel
    // which will kick off the filter chain.  Instead fake an event to make sure the buffered data
    // gets processed regardless.
    if (read_buffer_.length() > 0) {
      file_event_->activate(Event::FileReadyType::Read);
    }
  }
}

void ConnectionImpl::raiseEvent(ConnectionEvent event) {
  for (ConnectionCallbacks* callback : callbacks_) {
    // TODO(mattklein123): If we close while raising a connected event we should not raise further
    // connected events.
    callback->onEvent(event);
  }
}

bool ConnectionImpl::readEnabled() const { return state_ & InternalState::ReadEnabled; }

void ConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) { callbacks_.push_back(&cb); }

void ConnectionImpl::write(Buffer::Instance& data) {
  // NOTE: This is kind of a hack, but currently we don't support restart/continue on the write
  //       path, so we just pass around the buffer passed to us in this function. If we ever support
  //       buffer/restart/continue on the write path this needs to get more complicated.
  current_write_buffer_ = &data;
  FilterStatus status = filter_manager_.onWrite();
  current_write_buffer_ = nullptr;

  if (FilterStatus::StopIteration == status) {
    return;
  }

  if (data.length() > 0) {
    ENVOY_CONN_LOG(trace, "writing {} bytes", *this, data.length());
    // TODO(mattklein123): All data currently gets moved from the source buffer to the write buffer.
    // This can lead to inefficient behavior if writing a bunch of small chunks. In this case, it
    // would likely be more efficient to copy data below a certain size. VERY IMPORTANT: If this is
    // ever changed, read the comment in Ssl::ConnectionImpl::doWriteToSocket() VERY carefully.
    // That code assumes that we never change existing write_buffer_ chain elements between calls
    // to SSL_write(). That code will have to change if we ever copy here.
    write_buffer_->move(data);

    // Activating a write event before the socket is connected has the side-effect of tricking
    // doWriteReady into thinking the socket is connected. On OS X, the underlying write may fail
    // with a connection error if a call to write(2) occurs before the connection is completed.
    if (!(state_ & InternalState::Connecting)) {
      file_event_->activate(Event::FileReadyType::Write);
    }
  }
}

void ConnectionImpl::setBufferLimits(uint32_t limit) {
  read_buffer_limit_ = limit;

  // Due to the fact that writes to the connection and flushing data from the connection are done
  // asynchronously, we have the option of either setting the watermarks aggressively, and regularly
  // enabling/disabling reads from the socket, or allowing more data, but then not triggering
  // based on watermarks until 2x the data is buffered in the common case.  Given these are all soft
  // limits we err on the side of buffering more triggering watermark callbacks less often.
  //
  // Given the current implementation for straight up TCP proxying, the common case is reading
  // |limit| bytes through the socket, passing |limit| bytes to the connection (triggering the high
  // watermarks) and the immediately draining |limit| bytes to the socket (triggering the low
  // watermarks).  We avoid this by setting the high watermark to limit + 1 so a single read will
  // not trigger watermarks if the socket is not blocked.
  //
  // If the connection class is changed to write to the buffer and flush to the socket in the same
  // stack then instead of checking watermarks after the write and again after the flush it can
  // check once after both operations complete.  At that point it would be better to change the high
  // watermark from |limit + 1| to |limit| as the common case (move |limit| bytes, flush |limit|
  // bytes) would not trigger watermarks but a blocked socket (move |limit| bytes, flush 0 bytes)
  // would result in respecting the exact buffer limit.
  if (limit > 0) {
    static_cast<Buffer::WatermarkBuffer*>(write_buffer_.get())->setWatermarks(limit + 1);
  }
}

void ConnectionImpl::onLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", *this);
  ASSERT(above_high_watermark_);
  above_high_watermark_ = false;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionImpl::onHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", *this);
  ASSERT(!above_high_watermark_);
  above_high_watermark_ = true;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionImpl::onFileEvent(uint32_t events) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);

  if (state_ & InternalState::ImmediateConnectionError) {
    ENVOY_CONN_LOG(debug, "raising immediate connect error", *this);
    closeSocket(ConnectionEvent::RemoteClose);
    return;
  }

  if (state_ & InternalState::BindError) {
    ENVOY_CONN_LOG(debug, "raising bind error", *this);
    // Update stats here, rather than on bind failure, to give the caller a chance to
    // setConnectionStats.
    if (connection_stats_ && connection_stats_->bind_errors_) {
      connection_stats_->bind_errors_->inc();
    }
    closeSocket(ConnectionEvent::LocalClose);
    return;
  }

  if (events & Event::FileReadyType::Closed) {
    // We never ask for both early close and read at the same time. If we are reading, we want to
    // consume all available data.
    ASSERT(!(events & Event::FileReadyType::Read));
    ENVOY_CONN_LOG(debug, "remote early close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
    return;
  }

  if (events & Event::FileReadyType::Write) {
    onWriteReady();
  }

  // It's possible for a write event callback to close the socket (which will cause fd_ to be -1).
  // In this case ignore write event processing.
  if (fd_ != -1 && (events & Event::FileReadyType::Read)) {
    onReadReady();
  }
}

void ConnectionImpl::onReadReady() {
  ENVOY_CONN_LOG(trace, "read ready", *this);

  ASSERT(!(state_ & InternalState::Connecting));

  IoResult result = transport_socket_->doRead(read_buffer_);
  uint64_t new_buffer_size = read_buffer_.length();
  updateReadBufferStats(result.bytes_processed_, new_buffer_size);
  onRead(new_buffer_size);

  // The read callback may have already closed the connection.
  if (result.action_ == PostIoAction::Close) {
    ENVOY_CONN_LOG(debug, "remote close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
  }
}

void ConnectionImpl::onWriteReady() {
  ENVOY_CONN_LOG(trace, "write ready", *this);

  if (state_ & InternalState::Connecting) {
    int error;
    socklen_t error_size = sizeof(error);
    int rc = getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &error_size);
    ASSERT(0 == rc);
    UNREFERENCED_PARAMETER(rc);

    if (error == 0) {
      ENVOY_CONN_LOG(debug, "connected", *this);
      state_ &= ~InternalState::Connecting;
      transport_socket_->onConnected();
      // It's possible that we closed during the connect callback.
      if (state() != State::Open) {
        ENVOY_CONN_LOG(debug, "close during connected callback", *this);
        return;
      }
    } else {
      ENVOY_CONN_LOG(debug, "delayed connection error: {}", *this, error);
      closeSocket(ConnectionEvent::RemoteClose);
      return;
    }
  }

  IoResult result = transport_socket_->doWrite(*write_buffer_);
  uint64_t new_buffer_size = write_buffer_->length();
  updateWriteBufferStats(result.bytes_processed_, new_buffer_size);

  if (result.action_ == PostIoAction::Close) {
    // It is possible (though unlikely) for the connection to have already been closed during the
    // write callback. This can happen if we manage to complete the SSL handshake in the write
    // callback, raise a connected event, and close the connection.
    closeSocket(ConnectionEvent::RemoteClose);
  } else if ((state_ & InternalState::CloseWithFlush) && new_buffer_size == 0) {
    ENVOY_CONN_LOG(debug, "write flush complete", *this);
    closeSocket(ConnectionEvent::LocalClose);
  }
}

void ConnectionImpl::doConnect() {
  ENVOY_CONN_LOG(debug, "connecting to {}", *this, remote_address_->asString());
  int rc = remote_address_->connect(fd_);
  if (rc == 0) {
    // write will become ready.
    ASSERT(state_ & InternalState::Connecting);
  } else {
    ASSERT(rc == -1);
    if (errno == EINPROGRESS) {
      ASSERT(state_ & InternalState::Connecting);
      ENVOY_CONN_LOG(debug, "connection in progress", *this);
    } else {
      // read/write will become ready.
      state_ |= InternalState::ImmediateConnectionError;
      state_ &= ~InternalState::Connecting;
      ENVOY_CONN_LOG(debug, "immediate connection error: {}", *this, errno);
    }
  }

  // The local address can only be retrieved for IP connections.  Other
  // types, such as UDS, don't have a notion of a local address.
  if (remote_address_->type() == Address::Type::Ip) {
    local_address_ = Address::addressFromFd(fd_);
  }
}

void ConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  ASSERT(!connection_stats_);
  connection_stats_.reset(new ConnectionStats(stats));
}

void ConnectionImpl::updateReadBufferStats(uint64_t num_read, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_read, new_size, last_read_buffer_size_,
                                           connection_stats_->read_total_,
                                           connection_stats_->read_current_);
}

void ConnectionImpl::updateWriteBufferStats(uint64_t num_written, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_written, new_size, last_write_buffer_size_,
                                           connection_stats_->write_total_,
                                           connection_stats_->write_current_);
}

ClientConnectionImpl::ClientConnectionImpl(
    Event::DispatcherImpl& dispatcher, Address::InstanceConstSharedPtr address,
    const Network::Address::InstanceConstSharedPtr source_address)
    : ConnectionImpl(dispatcher, address->socket(Address::SocketType::Stream), address, nullptr,
                     source_address, false, false) {}

} // namespace Network
} // namespace Envoy
