#pragma once

#include "envoy/config/filter/http/rate_limit/v3alpha/rate_limit.pb.h"
#include "envoy/config/filter/http/rate_limit/v3alpha/rate_limit.pb.validate.h"

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
    : public Common::FactoryBase<envoy::config::filter::http::rate_limit::v3alpha::RateLimit> {
public:
  RateLimitFilterConfig() : FactoryBase(HttpFilterNames::get().RateLimit) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::rate_limit::v3alpha::RateLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
