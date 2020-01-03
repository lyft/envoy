#pragma once

#include <string>

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Http {

// A factory to create Http::ServerConnection instance for QUIC.
class QuicHttpServerConnectionFactory {
public:
  virtual ~QuicHttpServerConnectionFactory() {}

  virtual std::string name() const PURE;

  virtual std::unique_ptr<ServerConnection>
  createQuicServerConnection(Network::Connection& connection, ConnectionCallbacks& callbacks) PURE;

  static std::string category() {
    static const char FACTORY_CATEGORY[] = "quic_client_codec";
    return FACTORY_CATEGORY;
  }
};

// A factory to create Http::ClientConnection instance for QUIC.
class QuicHttpClientConnectionFactory {
public:
  virtual ~QuicHttpClientConnectionFactory() {}

  virtual std::string name() const PURE;

  virtual std::unique_ptr<ClientConnection>
  createQuicClientConnection(Network::Connection& connection, ConnectionCallbacks& callbacks) PURE;

  static std::string category() {
    static const char FACTORY_CATEGORY[] = "quic_server_codec";
    return FACTORY_CATEGORY;
  }
};

} // namespace Http
} // namespace Envoy
