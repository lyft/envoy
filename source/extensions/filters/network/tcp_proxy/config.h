#pragma once

#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxyFilter {

/**
 * Config registration for the tcp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class TcpProxyConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::tcp_proxy::v2::TcpProxy> {
public:
  TcpProxyConfigFactory() : FactoryBase(NetworkFilterNames::get().TCP_PROXY) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::tcp_proxy::v2::TcpProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace TcpProxyFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
