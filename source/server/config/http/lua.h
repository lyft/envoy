#pragma once

#include <string>

#include "envoy/config/filter/http/lua/v2/lua.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class LuaFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;

  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stat_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::lua::v2::Lua()};
  }

  std::string name() override { return Config::HttpFilterNames::get().LUA; }

private:
  HttpFilterFactoryCb createFilter(const envoy::config::filter::http::lua::v2::Lua& proto_config,
                                   const std::string&, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
