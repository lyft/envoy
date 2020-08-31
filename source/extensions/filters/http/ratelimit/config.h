#pragma once

#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

/**
 * Config registration for the rate limit filter. @see NamedHttpFilterConfigFactory.
 */
class RateLimitFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ratelimit::v3::RateLimit,
          envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute> {
public:
  RateLimitFilterConfig() : FactoryBase(HttpFilterNames::get().RateLimit) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute& proto_config,
      Server::Configuration::FactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
