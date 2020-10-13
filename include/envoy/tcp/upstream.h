#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace TcpProxy {

class GenericConnectionPoolCallbacks;
class GenericUpstream;

// An API for wrapping either a TCP or an HTTP connection pool.
class GenericConnPool : public Logger::Loggable<Logger::Id::router> {
public:
  virtual ~GenericConnPool() = default;

  /**
   * Called to create a TCP connection or HTTP stream for "CONNECT streams".
   *
   * The implementation is then responsible for calling either onGenericPoolReady or
   * onGenericPoolFailure on the supplied GenericConnectionPoolCallbacks.
   *
   * @param callbacks callbacks to communicate stream failure or creation on.
   */
  virtual void newStream(GenericConnectionPoolCallbacks* callbacks) PURE;

  /**
   * @return returns true if this connection pool is valid.
   */
  virtual bool valid() const PURE;
};

// An API for the UpstreamRequest to get callbacks from either an HTTP or TCP
// connection pool.
class GenericConnectionPoolCallbacks {
public:
  virtual ~GenericConnectionPoolCallbacks() = default;

  /**
   * Called when GenericConnPool::newStream has established a new stream.
   *
   * @param info supplies the stream info object associated with the upstream connection.
   * @param upstream supplies the generic upstream for the stream.
   * @param host supplies the description of the host that will carry the request.
   * @param upstream_local_address supplies the local address of the upstream connection.
   * @param ssl_info supplies the ssl information of the upstream connection.
   */
  virtual void onGenericPoolReady(StreamInfo::StreamInfo* info,
                                  std::unique_ptr<GenericUpstream>&& upstream,
                                  Upstream::HostDescriptionConstSharedPtr& host,
                                  const Network::Address::InstanceConstSharedPtr& local_address,
                                  Ssl::ConnectionInfoConstSharedPtr ssl_info) PURE;

  /**
   * Called to indicate a failure for GenericConnPool::newStream to establish a stream.
   *
   * @param reason supplies the failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    Upstream::HostDescriptionConstSharedPtr host) PURE;
};

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;

  /**
   * Enable/disable further data from this stream.
   *
   * @param disable true if the stream should be read disabled, false otherwise.
   * @return returns true if the disable is performed, false otherwise
             (e.g. if the connection is closed)
   */
  virtual bool readDisable(bool disable) PURE;

  /**
   * Encodes data upstream.
   * @param data supplies the data to encode. The data may be moved by the encoder.
   * @param end_stream supplies whether this is the last data to encode.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Adds a callback to be called when the data is sent to the kernel.
   * @param cb supplies the callback to be called
   */
  virtual void addBytesSentCallback(Network::Connection::BytesSentCb cb) PURE;

  /**
   * Called when an event is received on the downstream connection
   * @param event supplies the event which occurred.
   * @return the underlying ConnectionData if the event is not "Connected" and draining
             is supported for this upstream.
   */
  virtual Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) PURE;
};

} // namespace TcpProxy
} // namespace Envoy
