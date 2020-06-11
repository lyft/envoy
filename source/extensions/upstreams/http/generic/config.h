#pragma once

#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Generic {

/**
 * Config registration for the GenericConnPool. * @see Router::GenericConnPoolFactory
 */
class GenericGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return HttpConnectionPoolNames::get().Generic; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry, Envoy::Http::Protocol protocol,
                        Upstream::LoadBalancerContext* ctx) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::http::generic::v3::GenericConnectionPoolProto>();
  }
};

DECLARE_FACTORY(GenericGenericConnPoolFactory);

} // namespace Generic
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
