#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include "envoy/common/platform.h"
#include "envoy/network/socket.h"
#include "envoy/tcp/conn_info.h"

namespace Envoy {
namespace Tcp {

absl::optional<std::chrono::milliseconds>
ConnectionInfo::lastRoundTripTime(Envoy::Network::Socket& socket) {
  struct tcp_info ti;
  socklen_t len = sizeof(ti);
  if (SOCKET_FAILURE(socket_.getSocketOption(IPPROTO_TCP, TCP_INFO, &ti, &len).rc_)) {
    return std::chrono::milliseconds(ti.tcpi_rtt);
  }

  return {};
}

} // namespace Tcp
} // namespace Envoy
