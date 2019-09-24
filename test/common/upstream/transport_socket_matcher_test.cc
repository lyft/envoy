#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/metadata.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/utility.h"
#include "common/upstream/transport_socket_matcher.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

class FakeTransportSocketFactory : public Network::TransportSocketFactory {
public:
  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket,
      Network::TransportSocketPtr(Network::TransportSocketOptionsSharedPtr));
  FakeTransportSocketFactory(const std::string& id): id_(id) {}
  std::string id() const { return id_; }
private:
  const std::string id_;
};

class FooTransportSocketFactory : public Network::TransportSocketFactory,
        public Server::Configuration::UpstreamTransportSocketConfigFactory,
  Logger::Loggable<Logger::Id::upstream> 
  {
public:
  FooTransportSocketFactory() {}
  ~FooTransportSocketFactory() override {}

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket, Network::TransportSocketPtr(
        Network::TransportSocketOptionsSharedPtr));

  Network::TransportSocketFactoryPtr createTransportSocketFactory(
        const Protobuf::Message& proto,
        Server::Configuration::TransportSocketFactoryContext&) override {
    const auto* node= dynamic_cast<const envoy::api::v2::core::Node*>(&proto);
    std::string id = "default-foo";
    if (node->id() != "") {
      id = node->id();
    }
    return std::make_unique<FakeTransportSocketFactory>(id);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::api::v2::core::Node>();
  }

  std::string name() const override { return "foo"; }
};

class TransportSocketMatcherTest : public testing::Test {
public:
  TransportSocketMatcherTest(): mock_default_factory_("default"),
    stats_scope_(stats_store_.createScope("transport_socket_match.test"))
  {}

  void init(const std::vector<std::string>& match_yaml) {
    Protobuf::RepeatedPtrField<envoy::api::v2::Cluster_TransportSocketMatch> matches;
    for (const auto& yaml : match_yaml) {
      auto transport_socket_match = matches.Add();
      TestUtility::loadFromYaml(yaml, *transport_socket_match);
    }
    matcher_ = std::make_unique<TransportSocketMatcher>(
        matches, mock_factory_context_, mock_default_factory_, *stats_scope_);
  }

  void validate(const envoy::api::v2::core::Metadata& metadata, const std::string& expected) {
    auto& factory = matcher_->resolve("10.0.0.1", metadata);
    const auto* config_factory = dynamic_cast<const FakeTransportSocketFactory*>(&factory);
    EXPECT_EQ(expected, config_factory->id());
  }

protected:
  TransportSocketMatcherPtr matcher_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_context_;
  NiceMock<FakeTransportSocketFactory> mock_default_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopePtr stats_scope_;
};

TEST_F(TransportSocketMatcherTest, ReturnDefaultSocketFactoryWhenNoMatch) {
  init({R"EOF(
name: "enableFooSocket"
match:
  hasSidecar: "true"
transport_socket:
  name: "foo"
  config:
    id: "abc"
 )EOF"});

  envoy::api::v2::core::Metadata metadata;
  validate(metadata, "default");
}

TEST_F(TransportSocketMatcherTest, BasicMatch) {
  init({R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  config:
    id: "sidecar")EOF",
      R"EOF(
name: "http_socket"
match:
  protocol: "http"
transport_socket:
  name: "foo"
  config:
    id: "http"
 )EOF"});

  envoy::api::v2::core::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket: { sidecar: "true" } 
)EOF", metadata);

  validate(metadata, "sidecar");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket: { protocol: "http" } 
)EOF", metadata);
  validate(metadata, "http");
}

TEST_F(TransportSocketMatcherTest, MultipleMatchFirstWin) {
  init({R"EOF(
name: "sidecar_http_socket"
match:
  sidecar: "true"
  protocol: "http"
transport_socket:
  name: "foo"
  config:
    id: "sidecar_http"
 )EOF",
R"EOF(
name: "sidecar_socket"
match:
  sidecar: "true"
transport_socket:
  name: "foo"
  config:
    id: "sidecar"
 )EOF"
});
  envoy::api::v2::core::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket: { sidecar: "true", protocol: "http" }
)EOF", metadata);
  validate(metadata, "sidecar_http");
}

TEST_F(TransportSocketMatcherTest, MatchAllEndpointsFactory) {
  init({R"EOF(
name: "match_all"
match: {}
transport_socket:
  name: "foo"
  config:
    id: "match_all"
 )EOF"});
  envoy::api::v2::core::Metadata metadata;
  validate(metadata, "match_all");
  TestUtility::loadFromYaml(R"EOF(
filter_metadata:
  envoy.transport_socket: { random_label: "random_value" }
)EOF", metadata);
  validate(metadata, "match_all");
}

REGISTER_FACTORY(FooTransportSocketFactory,
    Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace
} // namespace Upstream
} // namespace Envoy
