#include "extensions/filters/network/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/network/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

Network::FilterFactoryCb RateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.domain().empty());
  ASSERT(proto_config.descriptors_size() > 0);

  ConfigSharedPtr filter_config(new Config(proto_config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  // When we introduce rate limit service config in filters, we should validate here that it matches
  // with bootstrap.
  Filters::Common::RateLimit::ClientPtr ratelimit_client =
      Filters::Common::RateLimit::ClientFactory::rateLimitClientFactory(
          context, Filters::Common::RateLimit::rateLimitConfig(context))
          ->create(std::chrono::milliseconds(timeout_ms), context);
  std::shared_ptr<Filter> filter =
      std::make_shared<Filter>(filter_config, std::move(ratelimit_client));
  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count. Moreover, keep in mind the capture list determines
  // destruction order.
  return [filter_config, filter, ratelimit_client](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(filter);
  };
}

Network::FilterFactoryCb
RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                            Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config;
  Envoy::Config::FilterJson::translateTcpRateLimitFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
