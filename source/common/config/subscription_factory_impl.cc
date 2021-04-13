#include "common/config/subscription_factory_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/config/filesystem_subscription_impl.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/new_grpc_mux_impl.h"
#include "common/config/type_to_endpoint.h"
#include "common/config/utility.h"
#include "common/config/xds_resource.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

SubscriptionFactoryImpl::SubscriptionFactoryImpl(
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    Api::Api& api)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm),
      validation_visitor_(validation_visitor), api_(api) {}

SubscriptionPtr SubscriptionFactoryImpl::subscriptionFromConfigSource(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoder& resource_decoder) {
  Config::Utility::checkLocalInfo(type_url, local_info_);
  std::unique_ptr<Subscription> result;
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (config.config_source_specifier_case()) {
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path(), api_);
    return std::make_unique<Config::FilesystemSubscriptionImpl>(
        dispatcher_, config.path(), callbacks, resource_decoder, stats, validation_visitor_, api_);
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                            api_config_source);
    const auto transport_api_version = Utility::getAndCheckTransportVersion(api_config_source);
    switch (api_config_source.api_type()) {
    case envoy::config::core::v3::ApiConfigSource::hidden_envoy_deprecated_UNSUPPORTED_REST_LEGACY:
      throw EnvoyException(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::config::core::v3::ApiConfigSource::REST:
      return std::make_unique<HttpSubscriptionImpl>(
          local_info_, cm_, api_config_source.cluster_names()[0], dispatcher_,
          api_.randomGenerator(), Utility::apiConfigSourceRefreshDelay(api_config_source),
          Utility::apiConfigSourceRequestTimeout(api_config_source),
          restMethod(type_url, transport_api_version), type_url, transport_api_version, callbacks,
          resource_decoder, stats, Utility::configSourceInitialFetchTimeout(config),
          validation_visitor_);
    case envoy::config::core::v3::ApiConfigSource::GRPC:
      return std::make_unique<GrpcSubscriptionImpl>(
          std::make_shared<Config::GrpcMuxImpl>(
              local_info_,
              Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                     api_config_source, scope, true)
                  ->create(),
              dispatcher_, sotwGrpcMethod(type_url, transport_api_version), transport_api_version,
              api_.randomGenerator(), scope, Utility::parseRateLimitSettings(api_config_source),
              api_config_source.set_node_on_first_message_only()),
          callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config),
          /*is_aggregated*/ false);
    case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
      return std::make_unique<GrpcSubscriptionImpl>(
          std::make_shared<Config::NewGrpcMuxImpl>(
              Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                             api_config_source, scope, true)
                  ->create(),
              dispatcher_, deltaGrpcMethod(type_url, transport_api_version), transport_api_version,
              api_.randomGenerator(), scope, Utility::parseRateLimitSettings(api_config_source),
              local_info_),
          callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config), false);
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
    if ( cm_.adsMux() == nullptr ) {
      throw EnvoyException("Sub-components (like SDS) of a primary cluster cannot be configured via ADS");
    }
    return std::make_unique<GrpcSubscriptionImpl>(
        cm_.adsMux(), callbacks, resource_decoder, stats, type_url, dispatcher_,
        Utility::configSourceInitialFetchTimeout(config), true);
  }
  default:
    throw EnvoyException(
        "Missing config source specifier in envoy::config::core::v3::ConfigSource");
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

SubscriptionPtr SubscriptionFactoryImpl::collectionSubscriptionFromUrl(
    const xds::core::v3::ResourceLocator& collection_locator,
    const envoy::config::core::v3::ConfigSource& /*config*/, absl::string_view /*type_url*/,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoder& resource_decoder) {
  std::unique_ptr<Subscription> result;
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (collection_locator.scheme()) {
  case xds::core::v3::ResourceLocator::FILE: {
    const std::string path = Http::Utility::localPathFromFilePath(collection_locator.id());
    Utility::checkFilesystemSubscriptionBackingPath(path, api_);
    return std::make_unique<Config::FilesystemCollectionSubscriptionImpl>(
        dispatcher_, path, callbacks, resource_decoder, stats, validation_visitor_, api_);
  }
  default:
    throw EnvoyException(fmt::format("Unsupported collection resource locator: {}",
                                     XdsResourceIdentifier::encodeUrl(collection_locator)));
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Config
} // namespace Envoy
