#pragma once

#include "envoy/config/config_provider_manager.h"
#include "envoy/config/core/v3alpha/config_source.pb.h"
#include "envoy/config/endpoint/v3alpha/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/service/discovery/v3alpha/discovery.pb.h"

#include "common/config/config_provider_impl.h"
#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class MockSubscriptionCallbacks : public SubscriptionCallbacks {
public:
  MockSubscriptionCallbacks() {
    ON_CALL(*this, resourceName(testing::_))
        .WillByDefault(testing::Invoke([](const ProtobufWkt::Any& resource) -> std::string {
          return resourceName_(TestUtility::anyConvert<ResourceType>(resource));
        }));
  }
  ~MockSubscriptionCallbacks() override = default;
  static std::string
  resourceName_(const envoy::config::endpoint::v3alpha::ClusterLoadAssignment& resource) {
    return resource.cluster_name();
  }
  template <class T> static std::string resourceName_(const T& resource) { return resource.name(); }

  MOCK_METHOD2_T(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string& version_info));
  MOCK_METHOD3_T(onConfigUpdate,
                 void(const Protobuf::RepeatedPtrField<
                          envoy::service::discovery::v3alpha::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info));
  MOCK_METHOD2_T(onConfigUpdateFailed,
                 void(Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException* e));
  MOCK_METHOD1_T(resourceName, std::string(const ProtobufWkt::Any& resource));
  MOCK_METHOD0(kickFallback, void());
};

class MockSubscription : public Subscription {
public:
  MOCK_METHOD1(start, void(const std::set<std::string>& resources));
  MOCK_METHOD1(updateResourceInterest, void(const std::set<std::string>& update_to_these_names));
  MOCK_METHOD1(fallback, void(const std::set<std::string>& resource_names));
};

class MockSubscriptionFactory : public SubscriptionFactory {
public:
  MockSubscriptionFactory();
  ~MockSubscriptionFactory() override;

  MOCK_METHOD4(subscriptionFromConfigSource,
               SubscriptionPtr(const envoy::config::core::v3alpha::ConfigSource& config,
                               absl::string_view type_url, Stats::Scope& scope,
                               SubscriptionCallbacks& callbacks));
  MOCK_METHOD0(messageValidationVisitor, ProtobufMessage::ValidationVisitor&());

  MockSubscription* subscription_{};
  SubscriptionCallbacks* callbacks_{};
};

class MockGrpcMuxWatch : public GrpcMuxWatch {
public:
  MockGrpcMuxWatch();
  ~MockGrpcMuxWatch() override;

  MOCK_METHOD0(cancel, void());
};

class MockGrpcMux : public GrpcMux {
public:
  MockGrpcMux();
  ~MockGrpcMux() override;

  MOCK_METHOD0(start, void());
  MOCK_METHOD4(subscribe_,
               GrpcMuxWatch*(const std::string& type_url, const std::set<std::string>& resources,
                             GrpcMuxCallbacks& callbacks, bool fallbacked));
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::set<std::string>& resources,
                            GrpcMuxCallbacks& callbacks, bool fallbacked = false) override;
  MOCK_METHOD1(pause, void(const std::string& type_url));
  MOCK_METHOD1(resume, void(const std::string& type_url));
  MOCK_CONST_METHOD1(paused, bool(const std::string& type_url));

  MOCK_METHOD5(addSubscription,
               void(const std::set<std::string>& resources, const std::string& type_url,
                    SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
                    std::chrono::milliseconds init_fetch_timeout));
  MOCK_METHOD2(updateResourceInterest,
               void(const std::set<std::string>& resources, const std::string& type_url));

  MOCK_METHOD5(addOrUpdateWatch,
               Watch*(const std::string& type_url, Watch* watch,
                      const std::set<std::string>& resources, SubscriptionCallbacks& callbacks,
                      std::chrono::milliseconds init_fetch_timeout));
  MOCK_METHOD2(removeWatch, void(const std::string& type_url, Watch* watch));
};

class MockGrpcMuxCallbacks : public GrpcMuxCallbacks {
public:
  MockGrpcMuxCallbacks();
  ~MockGrpcMuxCallbacks() override;

  MOCK_METHOD2(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                    const std::string& version_info));
  MOCK_METHOD2(onConfigUpdateFailed,
               void(Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException* e));
  MOCK_METHOD1(onTryFallback, void(Envoy::Config::ConfigUpdateFailureReason reason));
  MOCK_METHOD1(resourceName, std::string(const ProtobufWkt::Any& resource));
};

class MockGrpcStreamCallbacks
    : public GrpcStreamCallbacks<envoy::service::discovery::v3alpha::DiscoveryResponse> {
public:
  MockGrpcStreamCallbacks();
  ~MockGrpcStreamCallbacks() override;

  MOCK_METHOD0(onStreamEstablished, void());
  MOCK_METHOD1(onEstablishmentFailure, void(bool));
  MOCK_METHOD1(
      onDiscoveryResponse,
      void(std::unique_ptr<envoy::service::discovery::v3alpha::DiscoveryResponse>&& message));
  MOCK_METHOD0(onWriteable, void());
};

class MockConfigProviderManager : public ConfigProviderManager {
public:
  MockConfigProviderManager() = default;
  ~MockConfigProviderManager() override = default;

  MOCK_METHOD4(createXdsConfigProvider,
               ConfigProviderPtr(const Protobuf::Message& config_source_proto,
                                 Server::Configuration::FactoryContext& factory_context,
                                 const std::string& stat_prefix,
                                 const Envoy::Config::ConfigProviderManager::OptionalArg& optarg));
  MOCK_METHOD3(createStaticConfigProvider,
               ConfigProviderPtr(const Protobuf::Message& config_proto,
                                 Server::Configuration::FactoryContext& factory_context,
                                 const Envoy::Config::ConfigProviderManager::OptionalArg& optarg));
  MOCK_METHOD3(
      createStaticConfigProvider,
      ConfigProviderPtr(std::vector<std::unique_ptr<const Protobuf::Message>>&& config_protos,
                        Server::Configuration::FactoryContext& factory_context,
                        const Envoy::Config::ConfigProviderManager::OptionalArg& optarg));
};

} // namespace Config
} // namespace Envoy
