#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

/**
 * Config registration for the TcpConnPool. @see Router::GenericConnPoolFactory
 */
class TcpGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return HttpConnectionPoolNames::get().Tcp; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry, Envoy::Http::Protocol protocol,
                        Upstream::LoadBalancerContext* ctx) const override;
};

DECLARE_FACTORY(TcpGenericConnPoolFactory);

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
