#pragma once

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

/**
 * Config registration for the buffer filter.
 */
class BufferFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::buffer::v2::Buffer,
                                 envoy::config::filter::http::buffer::v2::BufferPerRoute> {
public:
  BufferFilterFactory() : FactoryBase(HttpFilterNames::get().BUFFER) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createTypedFilterFactoryFromProto(
      const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createTypedRouteSpecificFilterConfig(
      const envoy::config::filter::http::buffer::v2::BufferPerRoute&,
      Server::Configuration::FactoryContext&) override;
};

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
