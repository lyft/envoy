#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/network/connection_impl.h"
#include "common/network/connection_impl_base.h"
#include "common/stream_info/stream_info_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
class RandomPauseFilter;
class TestPauseFilter;

namespace Network {

#define DUMPEVENTS(tag, events)                                                                    \
  do {                                                                                             \
    std::string s;                                                                                 \
    if (events & Event::FileReadyType::Write) {                                                    \
      s += "WRITE|";                                                                               \
    }                                                                                              \
    if (events & Event::FileReadyType::Read) {                                                     \
      s += "READ|";                                                                                \
    }                                                                                              \
    if (events & Event::FileReadyType::Closed) {                                                   \
      s += "CLOSED|";                                                                              \
    }                                                                                              \
    ENVOY_LOG_MISC(debug, "lambdai: {} events {}", tag, s);                                        \
  } while (0)

class PeeringPipe {
public:
  virtual ~PeeringPipe() = default;
  void setPeer(PeeringPipe* peer) { peer_ = peer; }
  virtual void mayScheduleReadReady() PURE;
  virtual void closeSocket(ConnectionEvent close_type) PURE;

protected:
  PeeringPipe* peer_;
};


std::string eventDebugString(uint32_t events);

class ClientPipeImpl : public ConnectionImplBase,
                       public TransportSocketCallbacks,
                       virtual public ClientConnection,
                       public PeeringPipe,
                       public EventSchedulable {
public:
  ClientPipeImpl(Event::Dispatcher& dispatcher,
                 const Address::InstanceConstSharedPtr& remote_address,
                 const Address::InstanceConstSharedPtr& source_address,
                 Network::TransportSocketPtr transport_socket,
                 Network::ReadableSource& readable_source,
                 const Network::ConnectionSocket::OptionsSharedPtr& options);

  ~ClientPipeImpl() override;

  void setConnected() { scheduleWriteEvent(); }
  
  void resetSourceReadableFlag() {
    was_source_readable_ = readable_source_.isReadable();
  }
  void resetPeerWritableFlag() {
    was_peer_writable_ = isPeerWritable();
  }

  void scheduleNextEvent() override {
    if (!io_timer_->enabled()) {
      io_timer_->enableTimer(std::chrono::milliseconds(0));
    }
    ENVOY_LOG_MISC(debug, "lambdai: C{} scheduled persist events {} and ephermal events {}", id(),
                   eventDebugString(events_), eventDebugString(ephermal_events_));
  }

  //TODO(lambdai): check above watermark.
  // Check if peer writable regardless there is data to write.
  bool isPeerWritable() {
    return peer_ != nullptr;
  }
  // Check if source readable regardless the buffer is ready to read.
  bool isReadSourceReadable() {
    return (read_buffer_.length() > 0 || read_end_stream_ || readable_source_.isReadable());
  }
  bool isPeerClosed() {
    return readable_source_.isPeerShutDownWrite();
  }
  void enableWrite() {
    events_ = Event::FileReadyType::Write;
    if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  void enableWriteRead() {
    events_ = Event::FileReadyType::Write | Event::FileReadyType::Read;
    if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    if (isReadSourceReadable()) {
      ephermal_events_ |= Event::FileReadyType::Read;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  void enableWriteClose() {
    events_ = Event::FileReadyType::Write | Event::FileReadyType::Closed;
     if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    if (isPeerClosed()) {
      ephermal_events_ |= Event::FileReadyType::Closed;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  bool isReadEnabled() {
    return events_ | (Event::FileReadyType::Closed | Event::FileReadyType::Read);
  }
  bool isWriteEnabled() { return events_ | Event::FileReadyType::Write; }

  void scheduleWriteEvent() override {
    ephermal_events_ |= Event::FileReadyType::Write;
  }
  void scheduleReadEvent() override {
    ephermal_events_ |= Event::FileReadyType::Read;
  }
  void scheduleClosedEvent() override {
    ephermal_events_ |= Event::FileReadyType::Closed;
  }

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override;
  void close(ConnectionCloseType type) override;
  std::string nextProtocol() const override { return transport_socket_->protocol(); }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override { detect_early_close_ = value; }
  bool readEnabled() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return remote_address_;
  }
  const Address::InstanceConstSharedPtr& localAddress() const override { return source_address_; }
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return transport_socket_->ssl(); }
  State state() const override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return read_buffer_limit_; }
  bool localAddressRestored() const override { return true; }
  bool aboveHighWatermark() const override { return write_buffer_above_high_watermark_; }
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override { return options_; }
  absl::string_view requestedServerName() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override;

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  StreamBuffer getReadBuffer() override { return {read_buffer_, read_end_stream_}; }
  // Network::WriteBufferSource
  StreamBuffer getWriteBuffer() override {
    return {*current_write_buffer_, current_write_end_stream_};
  }

  // Network::TransportSocketCallbacks
  IoHandle& ioHandle() final { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  const IoHandle& ioHandle() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Connection& connection() override { return *this; }
  void raiseEvent(ConnectionEvent event) final;
  // Should the read buffer be drained?
  bool shouldDrainReadBuffer() override {
    return read_buffer_limit_ > 0 && read_buffer_.length() >= read_buffer_limit_;
  }
  // Mark read buffer ready to read in the event loop. This is used when yielding following
  // shouldDrainReadBuffer().
  // TODO(htuch): While this is the basis for also yielding to other connections to provide some
  // fair sharing of CPU resources, the underlying event loop does not make any fairness guarantees.
  // Reconsider how to make fairness happen.
  void setReadBufferReady() override {
    events_ |= Event::FileReadyType::Read;
    io_timer_->enableTimer(std::chrono::milliseconds(0));
  }
  void flushWriteBuffer() override;

  // Obtain global next connection ID. This should only be used in tests.
  static uint64_t nextGlobalIdForTest() { return next_global_id_; }

protected:
  // A convenience function which returns true if
  // 1) The read disable count is zero or
  // 2) The read disable count is one due to the read buffer being overrun.
  // In either case the consumer of the data would like to read from the buffer.
  // If the read count is greater than one, or equal to one when the buffer is
  // not overrun, then the consumer of the data has called readDisable, and does
  // not want to read.
  bool consumerWantsToRead();

  // Network::ConnectionImplBase
  void closeConnectionImmediately() override;

  // PeeringPipe
  void closeSocket(ConnectionEvent close_type) override;
  void mayScheduleReadReady() override;

  void onReadBufferLowWatermark();
  void onReadBufferHighWatermark();
  void onWriteBufferLowWatermark();
  void onWriteBufferHighWatermark();

  TransportSocketPtr transport_socket_;
  StreamInfo::StreamInfoImpl stream_info_;
  FilterManagerImpl filter_manager_;

  // Ensure that if the consumer of the data from this connection isn't
  // consuming, that the connection eventually stops reading from the wire.
  Buffer::WatermarkBuffer read_buffer_;
  // This must be a WatermarkBuffer, but as it is created by a factory the ConnectionImpl only has
  // a generic pointer.
  // It MUST be defined after the filter_manager_ as some filters may have callbacks that
  // write_buffer_ invokes during its clean up.
  Buffer::InstancePtr write_buffer_;
  uint32_t read_buffer_limit_ = 0;
  bool connecting_{false};
  ConnectionEvent immediate_error_event_{ConnectionEvent::Connected};

  void onReadReady();

  // Network::ClientConnection
  void connect() override;

private:
  friend class Envoy::RandomPauseFilter;
  friend class Envoy::TestPauseFilter;
  uint32_t checkTriggeredEvents();
  void onFileEvent();
  void onFileEvent(uint32_t events);
  void onRead(uint64_t read_buffer_size);
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  bool isOpen() const { return is_open_; }
  // Write data to the connection bypassing filter chain (optionally).
  void write(Buffer::Instance& data, bool end_stream, bool through_filter_chain);

  // Returns true iff end of stream has been both written and read.
  bool bothSidesHalfClosed();

  std::list<BytesSentCb> bytes_sent_callbacks_;
  // Tracks the number of times reads have been disabled. If N different components call
  // readDisabled(true) this allows the connection to only resume reads when readDisabled(false)
  // has been called N times.
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  Buffer::Instance* current_write_buffer_{};
  uint32_t read_disable_count_{0};
  bool write_buffer_above_high_watermark_ : 1;
  bool detect_early_close_ : 1;
  bool enable_half_close_ : 1;
  bool read_end_stream_raised_ : 1;
  bool read_end_stream_ : 1;
  bool write_end_stream_ : 1;
  bool current_write_end_stream_ : 1;
  bool dispatch_buffered_data_ : 1;
  // The flag of isOpen replacing the one in io handle.
  bool is_open_{true};

  const Address::InstanceConstSharedPtr remote_address_;
  const Address::InstanceConstSharedPtr source_address_;
  const Network::ConnectionSocket::OptionsSharedPtr options_;

  Network::ReadableSource& readable_source_;
  // This timer is used to trigger the next event.
  Event::TimerPtr io_timer_;
  // Persistent events.
  uint32_t events_{0};
  // Set by activate and cleared when the callbacks are triggered.
  uint32_t ephermal_events_{0};
  bool was_source_readable_{false};
  bool was_peer_writable_{false};
};
class ServerPipeImpl : public ConnectionImplBase,
                       public TransportSocketCallbacks,
                       public PeeringPipe,
                       public EventSchedulable {

public:
  ServerPipeImpl(Event::Dispatcher& dispatcher,
                 const Address::InstanceConstSharedPtr& remote_address,
                 const Address::InstanceConstSharedPtr& source_address,
                 Network::TransportSocketPtr transport_socket,
                 Network::ReadableSource& readable_source,
                 const Network::ConnectionSocket::OptionsSharedPtr& options);

  ~ServerPipeImpl() override;

  void setConnected() { onWriteReady(); }
  
  void resetSourceReadableFlag() {
    was_source_readable_ = readable_source_.isReadable();
  }
  void resetPeerWritableFlag() {
    was_peer_writable_ = isPeerWritable();
  }

  void scheduleWriteEvent() override {
    ephermal_events_ |= Event::FileReadyType::Write;
  }
  void scheduleReadEvent() override {
    ephermal_events_ |= Event::FileReadyType::Read;
  }
  void scheduleClosedEvent() override {
    ephermal_events_ |= Event::FileReadyType::Closed;
  }
  void scheduleNextEvent() override {
    if (!io_timer_->enabled()) {
      io_timer_->enableTimer(std::chrono::milliseconds(0));
    }
    ENVOY_LOG_MISC(debug, "lambdai: C{} scheduled persist events {} and ephermal events {}", id(),
                   eventDebugString(events_), eventDebugString(ephermal_events_));
  }
  //TODO(lambdai): check above watermark.
  // Check if peer writable regardless there is data to write.
  bool isPeerWritable() {
    return peer_ != nullptr;
  }
  // Check if source readable regardless the buffer is ready to read.
  bool isReadSourceReadable() {
    return (read_buffer_.length() > 0 || read_end_stream_ || readable_source_.isReadable());
  }
  bool isPeerClosed() {
    return readable_source_.isPeerShutDownWrite();
  }
  void enableWrite() {
    events_ = Event::FileReadyType::Write;
    if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  void enableWriteRead() {
    events_ = Event::FileReadyType::Write | Event::FileReadyType::Read;
    if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    if (isReadSourceReadable()) {
      ephermal_events_ |= Event::FileReadyType::Read;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  void enableWriteClose() {
    events_ = Event::FileReadyType::Write | Event::FileReadyType::Closed;
     if (isPeerWritable()) {
      ephermal_events_ |= Event::FileReadyType::Write;
      scheduleNextEvent();
    }
    if (isPeerClosed()) {
      ephermal_events_ |= Event::FileReadyType::Closed;
      scheduleNextEvent();
    }
    DUMPEVENTS(__FUNCTION__, events_);
  }
  bool isReadEnabled() {
    return events_ | (Event::FileReadyType::Closed | Event::FileReadyType::Read);
  }
  bool isWriteEnabled() { return events_ | Event::FileReadyType::Write; }

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override;
  void close(ConnectionCloseType type) override;
  std::string nextProtocol() const override { return transport_socket_->protocol(); }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override { detect_early_close_ = value; }
  bool readEnabled() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return remote_address_;
  }
  const Address::InstanceConstSharedPtr& localAddress() const override { return source_address_; }
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return transport_socket_->ssl(); }
  State state() const override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return read_buffer_limit_; }
  bool localAddressRestored() const override { return true; }
  bool aboveHighWatermark() const override { return write_buffer_above_high_watermark_; }
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override { return options_; }
  absl::string_view requestedServerName() const override {
    // TODO(lambdai): requested server name is required by tcp proxy.
    return "";
  }
  StreamInfo::StreamInfo& streamInfo() override { return *stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return *stream_info_; }
  absl::string_view transportFailureReason() const override;

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  StreamBuffer getReadBuffer() override { return {read_buffer_, read_end_stream_}; }
  // Network::WriteBufferSource
  StreamBuffer getWriteBuffer() override {
    return {*current_write_buffer_, current_write_end_stream_};
  }

  // Network::TransportSocketCallbacks
  IoHandle& ioHandle() final { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  const IoHandle& ioHandle() const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  Connection& connection() override { return *this; }
  void raiseEvent(ConnectionEvent event) final;
  // Should the read buffer be drained?
  bool shouldDrainReadBuffer() override {
    return read_buffer_limit_ > 0 && read_buffer_.length() >= read_buffer_limit_;
  }
  // Mark read buffer ready to read in the event loop. This is used when yielding following
  // shouldDrainReadBuffer().
  // TODO(htuch): While this is the basis for also yielding to other connections to provide some
  // fair sharing of CPU resources, the underlying event loop does not make any fairness guarantees.
  // Reconsider how to make fairness happen.
  // TODO(lambdai):
  void setReadBufferReady() override {
    events_ |= Event::FileReadyType::Read;
    io_timer_->enableTimer(std::chrono::milliseconds(0));
  }
  void flushWriteBuffer() override;

  // Obtain global next connection ID. This should only be used in tests.
  static uint64_t nextGlobalIdForTest() { return next_global_id_; }

  void setStreamInfo(StreamInfo::StreamInfo* stream_info) { stream_info_ = stream_info; }
  void onReadReady();

protected:
  // A convenience function which returns true if
  // 1) The read disable count is zero or
  // 2) The read disable count is one due to the read buffer being overrun.
  // In either case the consumer of the data would like to read from the buffer.
  // If the read count is greater than one, or equal to one when the buffer is
  // not overrun, then the consumer of the data has called readDisable, and does
  // not want to read.
  bool consumerWantsToRead();

  // Network::ConnectionImplBase
  void closeConnectionImmediately() override;

  // PeeringPipe
  void closeSocket(ConnectionEvent close_type) override;
  void mayScheduleReadReady() override;

  void onReadBufferLowWatermark();
  void onReadBufferHighWatermark();
  void onWriteBufferLowWatermark();
  void onWriteBufferHighWatermark();

  TransportSocketPtr transport_socket_;
  StreamInfo::StreamInfo* stream_info_{};
  FilterManagerImpl filter_manager_;

  // Ensure that if the consumer of the data from this connection isn't
  // consuming, that the connection eventually stops reading from the wire.
  Buffer::WatermarkBuffer read_buffer_;
  // This must be a WatermarkBuffer, but as it is created by a factory the ConnectionImpl only has
  // a generic pointer.
  // It MUST be defined after the filter_manager_ as some filters may have callbacks that
  // write_buffer_ invokes during its clean up.
  Buffer::InstancePtr write_buffer_;
  uint32_t read_buffer_limit_ = 0;
  ConnectionEvent immediate_error_event_{ConnectionEvent::Connected};

private:
  friend class Envoy::RandomPauseFilter;
  friend class Envoy::TestPauseFilter;
  // Return edge triggered events.
  uint32_t checkTriggeredEvents();
  void onFileEvent();
  void onFileEvent(uint32_t events);
  void onRead(uint64_t read_buffer_size);
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  bool isOpen() const { return is_open_; }
  // Write data to the connection bypassing filter chain (optionally).
  void write(Buffer::Instance& data, bool end_stream, bool through_filter_chain);

  // Returns true iff end of stream has been both written and read.
  bool bothSidesHalfClosed();

  std::list<BytesSentCb> bytes_sent_callbacks_;
  // Tracks the number of times reads have been disabled. If N different components call
  // readDisabled(true) this allows the connection to only resume reads when readDisabled(false)
  // has been called N times.
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  Buffer::Instance* current_write_buffer_{};
  uint32_t read_disable_count_{0};
  bool write_buffer_above_high_watermark_ : 1;
  bool detect_early_close_ : 1;
  bool enable_half_close_ : 1;
  bool read_end_stream_raised_ : 1;
  bool read_end_stream_ : 1;
  bool write_end_stream_ : 1;
  bool current_write_end_stream_ : 1;
  bool dispatch_buffered_data_ : 1;
  // The flag of isOpen replacing the one in io handle.
  bool is_open_{true};

  const Address::InstanceConstSharedPtr remote_address_;
  const Address::InstanceConstSharedPtr source_address_;
  const Network::ConnectionSocket::OptionsSharedPtr options_;

  Network::ReadableSource& readable_source_;
  // This timer is used to trigger the next event.
  Event::TimerPtr io_timer_;
  // Persistent events.
  uint32_t events_{0};
  // Set by activate and cleared when the callbacks are triggered.
  uint32_t ephermal_events_{0};
  bool was_source_readable_{true};
  bool was_peer_writable_{false};
};

} // namespace Network
} // namespace Envoy
