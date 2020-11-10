#pragma once

#include "common/http/conn_pool_base.h"

namespace Envoy {
namespace Http {

// An HTTP connection pool which supports both HTTP/1 and HTTP/2 based on ALPN
class HttpConnPoolImplMixed : public HttpConnPoolImplBase {
public:
  HttpConnPoolImplMixed(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                        Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                        const Network::ConnectionSocket::OptionsSharedPtr& options,
                        const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
      : HttpConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                             transport_socket_options, random_generator,
                             {Protocol::Http2, Protocol::Http11}) {}

  Http::Protocol protocol() const override {
    // This is a pure debug check to ensure call sites defer protocol() calls
    // until ALPN has a chance to be negotiated.
    ASSERT(connected_);
    return protocol_;
  }
  Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override;
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;

  void onConnected(Envoy::ConnectionPool::ActiveClient& client) override;

private:
  bool connected_{};
  // Default to HTTP/1, as servers which don't support ALPN are probably HTTP/1 only.
  Http::Protocol protocol_ = Protocol::Http11;
};

} // namespace Http
} // namespace Envoy
