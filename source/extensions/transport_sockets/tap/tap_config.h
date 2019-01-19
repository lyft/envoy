#pragma once

#include "extensions/common/tap/tap.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

/**
 * Per-socket tap implementation. Abstractly handles all socket lifecycle events in order to tap
 * if the configuration matches.
 */
class PerSocketTapper {
public:
  virtual ~PerSocketTapper() = default;

  /**
   * Called when the socket is closed.
   * @param event supplies the close type.
   */
  virtual void closeSocket(Network::ConnectionEvent event) PURE;

  /**
   * Called when data is read from the underlying transport.
   * @param data supplies the read data.
   */
  virtual void onRead(absl::string_view data) PURE;

  /**
   * Called when data is written to the underlying transport.
   * @param data supplies the written data.
   * @param end_stream supplies whether this is the end of socket writes.
   */
  virtual void onWrite(absl::string_view data, bool end_stream) PURE;
};

using PerSocketTapperPtr = std::unique_ptr<PerSocketTapper>;

/**
 * Abstract socket tap configuration.
 */
class SocketTapConfig : public Extensions::Common::Tap::TapConfig {
public:
  /**
   * @return a new per-socket tapper which is used to handle tapping of a discrete socket.
   * @param connection supplies the underlying network connection.
   */
  virtual PerSocketTapperPtr createPerSocketTapper(const Network::Connection& connection) PURE;
};

using SocketTapConfigSharedPtr = std::shared_ptr<SocketTapConfig>;

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
