#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Default {

/**
 * Config registration for the DefaultConnPool. * @see Router::GenericConnPoolFactory
 */
class DefaultGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return HttpConnectionPoolNames::get().Default; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry, Envoy::Http::Protocol protocol,
                        Upstream::LoadBalancerContext* ctx) const override;
};

DECLARE_FACTORY(DefaultGenericConnPoolFactory);

} // namespace Default
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
