#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/address.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/ssl/connection.h"

namespace Envoy {
namespace Event {
class Dispatcher;
}

namespace Network {

/**
 * Events that occur on a connection.
 */
enum class ConnectionEvent {
  RemoteClose,
  LocalClose,
  Connected,
};

/**
 * Connections have both a read and write buffer.
 */
enum class ConnectionBufferType { Read, Write };

/**
 * Network level callbacks that happen on a connection.
 */
class ConnectionCallbacks {
public:
  virtual ~ConnectionCallbacks() {}

  /**
   * Callback for connection events.
   * @param events supplies the ConnectionEvent that occurred.
   */
  virtual void onEvent(ConnectionEvent event) PURE;

  /**
   * Called when the write buffer for a connection goes over its high watermark.
   */
  virtual void onAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the write buffer for a connection goes from over its high
   * watermark to under its low watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() PURE;
};

/**
 * Type of connection close to perform.
 */
enum class ConnectionCloseType {
  FlushWrite, // Flush pending write data before raising ConnectionEvent::LocalClose
  NoFlush     // Do not flush any pending data and immediately raise ConnectionEvent::LocalClose
};

/**
 * An abstract raw connection. Free the connection or call close() to disconnect.
 */
class Connection : public Event::DeferredDeletable, public FilterManager {
public:
  enum class State { Open, Closing, Closed };

  /**
   * Callback function for when bytes have been sent by a connection.
   * @param bytes_sent supplies the number of bytes written to the connection.
   */
  typedef std::function<void(uint64_t bytes_sent)> BytesSentCb;

  struct ConnectionStats {
    Stats::Counter& read_total_;
    Stats::Gauge& read_current_;
    Stats::Counter& write_total_;
    Stats::Gauge& write_current_;
    // Counter* as this is an optional counter. Bind errors will not be tracked if this is nullptr.
    Stats::Counter* bind_errors_;
  };

  virtual ~Connection() {}

  /**
   * Register callbacks that fire when connection events occur.
   */
  virtual void addConnectionCallbacks(ConnectionCallbacks& cb) PURE;

  /**
   * Register for callback everytime bytes are written to the underlying TransportSocket.
   */
  virtual void addBytesSentCallback(BytesSentCb cb) PURE;

  /**
   * Close the connection.
   */
  virtual void close(ConnectionCloseType type) PURE;

  /**
   * @return Event::Dispatcher& the dispatcher backing this connection.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return uint64_t the unique local ID of this connection.
   */
  virtual uint64_t id() const PURE;

  /**
   * @return std::string the next protocol to use as selected by network level negotiation. (E.g.,
   *         ALPN). If network level negotation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string nextProtocol() const PURE;

  /**
   * Enable/Disable TCP NO_DELAY on the connection.
   */
  virtual void noDelay(bool enable) PURE;

  /**
   * Disable socket reads on the connection, applying external back pressure. When reads are
   * enabled again if there is data still in the input buffer it will be redispatched through
   * the filter chain.
   * @param disable supplies TRUE is reads should be disabled, FALSE if they should be enabled.
   */
  virtual void readDisable(bool disable) PURE;

  /**
   * Set if Envoy should detect TCP connection close when readDisable(true) is called.
   * By default, this is true on newly created connections.
   *
   * @param should_detect supplies if disconnects should be detected when the connection has been
   * read disabled
   */
  virtual void detectEarlyCloseWhenReadDisabled(bool should_detect) PURE;

  /**
   * @return bool whether reading is enabled on the connection.
   */
  virtual bool readEnabled() const PURE;

  /**
   * @return The address of the remote client. Note that this method will never return nullptr.
   */
  virtual const Network::Address::InstanceConstSharedPtr& remoteAddress() const PURE;

  /**
   * @return the local address of the connection. For client connections, this is the origin
   * address. For server connections, this is the local destination address. For server connections
   * it can be different from the proxy address if the downstream connection has been redirected or
   * the proxy is operating in transparent mode. Note that this method will never return nullptr.
   */
  virtual const Network::Address::InstanceConstSharedPtr& localAddress() const PURE;

  /**
   * Set the stats to update for various connection state changes. Note that for performance reasons
   * these stats are eventually consistent and may not always accurately represent the connection
   * state at any given point in time.
   */
  virtual void setConnectionStats(const ConnectionStats& stats) PURE;

  /**
   * @return the SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual Ssl::Connection* ssl() PURE;

  /**
   * @return the const SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual const Ssl::Connection* ssl() const PURE;

  /**
   * @return State the current state of the connection.
   */
  virtual State state() const PURE;

  /**
   * Write data to the connection. Will iterate through downstream filters with the buffer if any
   * are installed.
   */
  virtual void write(Buffer::Instance& data) PURE;

  /**
   * Set a soft limit on the size of buffers for the connection.
   * For the read buffer, this limits the bytes read prior to flushing to further stages in the
   * processing pipeline.
   * For the write buffer, it sets watermarks. When enough data is buffered it triggers a call to
   * onAboveWriteBufferHighWatermark, which allows subscribers to enforce flow control by disabling
   * reads on the socket funneling data to the write buffer. When enough data is drained from the
   * write buffer, onBelowWriteBufferHighWatermark is called which similarly allows subscribers
   * resuming reading.
   */
  virtual void setBufferLimits(uint32_t limit) PURE;

  /**
   * Get the value set with setBufferLimits.
   */
  virtual uint32_t bufferLimit() const PURE;

  /**
   * @return boolean telling if the connection's local address has been restored to an original
   *         destination address, rather than the address the connection was accepted at.
   */
  virtual bool localAddressRestored() const PURE;

  /**
   * @return boolean telling if the connection is currently above the high watermark.
   */
  virtual bool aboveHighWatermark() const PURE;

  /**
   * Get the socket options set on this connection.
   */
  virtual const ConnectionSocket::OptionsSharedPtr& socketOptions() const PURE;
};

typedef std::unique_ptr<Connection> ConnectionPtr;

/**
 * Connections capable of outbound connects.
 */
class ClientConnection : public virtual Connection {
public:
  /**
   * Connect to a remote host. Errors or connection events are reported via the event callback
   * registered via addConnectionCallbacks().
   */
  virtual void connect() PURE;
};

typedef std::unique_ptr<ClientConnection> ClientConnectionPtr;

} // namespace Network
} // namespace Envoy
