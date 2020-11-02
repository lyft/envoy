#pragma once

#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"
#include "common/network/io_socket_error_impl.h"

#include "extensions/io_socket/buffered_io_socket/peer_buffer.h"
#include "extensions/io_socket/buffered_io_socket/user_space_file_event_impl.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {
/**
 * Network::IoHandle implementation which provides a buffer as data source. It is designed to used
 * by Network::ConnectionImpl. Some known limitations include
 * 1. It doesn't not include a file descriptor. Do not use "fdDoNotUse".
 * 2. It doesn't support socket options. Wrap this in ConnectionSocket and implement the socket
 * getter/setter options.
 * 3. It doesn't support UDP interface.
 * 4. The peer BufferedIoSocket must be scheduled in the same thread to avoid data race because
 *    BufferedIoSocketHandle mutates the state of peer handle and no lock is introduced.
 */
class BufferedIoSocketHandleImpl final : public Network::IoHandle,
                                         public ReadWritable,
                                         protected Logger::Loggable<Logger::Id::io> {
public:
  BufferedIoSocketHandleImpl();

  ~BufferedIoSocketHandleImpl() override;

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override { return INVALID_SOCKET; }
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer, uint64_t max_length) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Network::Address::InstanceConstSharedPtr localAddress() override;
  Network::Address::InstanceConstSharedPtr peerAddress() override;

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  Network::IoHandlePtr duplicate() override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;

  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override { return absl::nullopt; }

  // WritablePeer
  void setWriteEnd() override { read_end_stream_ = true; }
  bool isWriteEndSet() override { return read_end_stream_; }
  void maybeSetNewData() override {
    ENVOY_LOG(trace, "{} on socket {}", __FUNCTION__, static_cast<void*>(this));
    if (user_file_event_) {
      user_file_event_->activate(Event::FileReadyType::Read);
    }
  }
  void onPeerDestroy() override {
    writable_peer_ = nullptr;
    write_shutdown_ = true;
  }
  void onPeerBufferWritable() override {
    if (user_file_event_) {
      user_file_event_->activate(Event::FileReadyType::Write);
    }
  }
  bool isWritable() const override { return !over_high_watermark_; }
  bool isPeerWritable() const override {
    return writable_peer_ != nullptr && !writable_peer_->isWriteEndSet() &&
           writable_peer_->isWritable();
  }
  Buffer::Instance* getWriteBuffer() override { return &pending_received_data_; }

  // ReadWritable
  bool isPeerShutDownWrite() const override { return read_end_stream_; }
  bool isReadable() const override {
    return isPeerShutDownWrite() || pending_received_data_.length() > 0;
  }

  // Set the peer which will populate the owned pending_received_data.
  void setWritablePeer(WritablePeer* writable_peer) {
    // Swapping writable peer is undefined behavior.
    ASSERT(!writable_peer_);
    ASSERT(!write_shutdown_);
    writable_peer_ = writable_peer;
  }

private:
  // Support isOpen() and close(). Network::IoHandle owner must invoke close() to avoid potential
  // resource leak.
  bool closed_{false};

  // The attached file event with this socket. The event is not owned by the socket in the current
  // Envoy model. Multiple events can be created during the life time of this IO handle but at any
  // moment at most 1 event is attached.
  std::unique_ptr<UserSpaceFileEventImpl> user_file_event_;

  // True if pending_received_data_ is not addable. Note that pending_received_data_ may have
  // pending data to drain.
  bool read_end_stream_{false};

  // The buffer owned by this socket. This buffer is populated by the write operations of the peer
  // socket and drained by read operations of this socket.
  Buffer::WatermarkBuffer pending_received_data_;

  // Destination of the write(). The value remains non-null until the peer is closed.
  WritablePeer* writable_peer_{nullptr};

  // The flag whether the peer is valid. Any write attempt must check this flag.
  bool write_shutdown_{false};

  // The watermark state of pending_received_data_.
  bool over_high_watermark_{false};
};
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy