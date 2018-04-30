#pragma once

#include <fstream>

#include "envoy/extensions/transport_socket/capture/v2/capture.pb.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Capture {

class CaptureSocket : public Network::TransportSocket {
public:
  CaptureSocket(const std::string& path_prefix, bool text_format,
                Network::TransportSocketPtr&& transport_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::Connection* ssl() override;
  const Ssl::Connection* ssl() const override;

private:
  const std::string& path_prefix_;
  const bool text_format_;
  envoy::extensions::transport_socket::capture::v2::Trace trace_;
  Network::TransportSocketPtr transport_socket_;
  Network::TransportSocketCallbacks* callbacks_{};
};

class CaptureSocketFactory : public Network::TransportSocketFactory {
public:
  CaptureSocketFactory(const std::string& path_prefix, bool text_format,
                       Network::TransportSocketFactoryPtr&& transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr createTransportSocket() const override;
  bool implementsSecureTransport() const override;

private:
  const std::string path_prefix_;
  const bool text_format_;
  Network::TransportSocketFactoryPtr transport_socket_factory_;
};

} // namespace Capture
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
