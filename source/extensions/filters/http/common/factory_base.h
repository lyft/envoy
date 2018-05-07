#pragma once

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Common base class for HTTP filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class RouteConfigProto = ConfigProto>
class FactoryBase : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  // Server::Configuration::NamedHttpFilterConfigFactory
  Http::FilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) override {
    // Only used in v1 filters.
    NOT_IMPLEMENTED;
  }

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createTypedFilterFactoryFromProto(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config), stats_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigProto()};
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return ProtobufTypes::MessagePtr{new RouteConfigProto()};
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::FactoryContext& context) override {
    return createTypedRouteSpecificFilterConfig(
        MessageUtil::downcastAndValidate<const RouteConfigProto&>(proto_config), context);
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Http::FilterFactoryCb
  createTypedFilterFactoryFromProto(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createTypedRouteSpecificFilterConfig(const RouteConfigProto&,
                                       Server::Configuration::FactoryContext&) {
    return nullptr;
  }

  const std::string name_;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
