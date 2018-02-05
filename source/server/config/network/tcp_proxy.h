#pragma once

#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the tcp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class TcpProxyConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                      FactoryContext& context) override;
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                             FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::unique_ptr<envoy::config::filter::network::tcp_proxy::v2::TcpProxy>(
        new envoy::config::filter::network::tcp_proxy::v2::TcpProxy());
  }

  std::string name() override { return Config::NetworkFilterNames::get().TCP_PROXY; }

private:
  NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
               FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
