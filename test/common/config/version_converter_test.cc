#include "envoy/api/v2/cluster.pb.h"
#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/cluster/v3alpha/cluster.pb.h"
#include "envoy/service/discovery/v3alpha/discovery.pb.h"

#include "common/config/api_version.h"
#include "common/config/version_converter.h"
#include "common/protobuf/well_known.h"

#include "test/common/config/version_converter.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

bool hasOriginalTypeInformation(const Protobuf::Message& message) {
  const Protobuf::Reflection* reflection = message.GetReflection();
  const auto& unknown_field_set = reflection->GetUnknownFields(message);
  for (int i = 0; i < unknown_field_set.field_count(); ++i) {
    const auto& unknown_field = unknown_field_set.field(i);
    if (unknown_field.number() == ProtobufWellKnown::OriginalTypeFieldNumber) {
      return true;
    }
  }
  return false;
}

// Wire-style upgrading between versions.
TEST(VersionConverterTest, Upgrade) {
  // Create a v2 Cluster message with some fields set.
  API_NO_BOOST(envoy::api::v2::Cluster) source;
  source.add_hosts();
  source.mutable_load_assignment()->set_cluster_name("bar");
  source.mutable_eds_cluster_config()->set_service_name("foo");
  source.set_drain_connections_on_host_removal(true);
  // Upgrade to a v3 Cluster.
  API_NO_BOOST(envoy::config::cluster::v3alpha::Cluster) dst;
  VersionConverter::upgrade(source, dst);
  // Verify fields in v3 Cluster.
  EXPECT_TRUE(hasOriginalTypeInformation(dst));
  EXPECT_FALSE(dst.hosts().empty());
  EXPECT_FALSE(hasOriginalTypeInformation(dst.hosts(0)));
  EXPECT_EQ("bar", dst.load_assignment().cluster_name());
  EXPECT_FALSE(hasOriginalTypeInformation(dst.load_assignment()));
  EXPECT_EQ("foo", dst.eds_cluster_config().service_name());
  EXPECT_TRUE(hasOriginalTypeInformation(dst.eds_cluster_config()));
  EXPECT_TRUE(dst.ignore_health_on_host_removal());
  // Recover a v2 Cluster from the v3 Cluster using original type information.
  auto original_dynamic_msg = VersionConverter::recoverOriginal(dst);
  const auto& original_msg = *original_dynamic_msg->msg_;
  EXPECT_EQ("envoy.api.v2.Cluster", original_msg.GetDescriptor()->full_name());
  // Ensure that we erased any original type information and have the original
  // message.
  EXPECT_THAT(original_msg, ProtoEq(source));
  // Verify that sub-messages work with VersionConverter::recoverOriginal, i.e.
  // we are propagating original type information.
  auto original_dynamic_sub_msg = VersionConverter::recoverOriginal(dst.eds_cluster_config());
  const auto& original_sub_msg = *original_dynamic_sub_msg->msg_;
  EXPECT_THAT(original_sub_msg, ProtoEq(source.eds_cluster_config()));
}

// Verify that VersionUtil::scrubHiddenEnvoyDeprecated recursively scrubs any
// deprecated fields.
TEST(VersionConverterTest, ScrubHiddenEnvoyDeprecated) {
  API_NO_BOOST(envoy::config::cluster::v3alpha::Cluster) msg;
  msg.set_name("foo");
  msg.mutable_hidden_envoy_deprecated_tls_context();
  EXPECT_TRUE(msg.has_hidden_envoy_deprecated_tls_context());
  msg.mutable_load_balancing_policy()->add_policies()->mutable_hidden_envoy_deprecated_config();
  EXPECT_TRUE(msg.load_balancing_policy().policies(0).has_hidden_envoy_deprecated_config());
  VersionUtil::scrubHiddenEnvoyDeprecated(msg);
  EXPECT_EQ("foo", msg.name());
  EXPECT_FALSE(msg.has_hidden_envoy_deprecated_tls_context());
  EXPECT_FALSE(msg.load_balancing_policy().policies(0).has_hidden_envoy_deprecated_config());
}

// Validate that we can sensibly provide a JSON wire interpretation of messages
// such as DiscoveryRequest based on transport API version.
TEST(VersionConverter, GetJsonStringFromMessage) {
  API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) discovery_request;
  discovery_request.mutable_node()->set_hidden_envoy_deprecated_build_version("foo");
  discovery_request.mutable_node()->set_user_agent_name("bar");
  const std::string v2_discovery_request = VersionConverter::getJsonStringFromMessage(
      discovery_request, envoy::config::core::v3alpha::ApiVersion::V2);
  EXPECT_EQ("{\"node\":{\"build_version\":\"foo\",\"user_agent_name\":\"bar\"}}",
            v2_discovery_request);
  const std::string auto_discovery_request = VersionConverter::getJsonStringFromMessage(
      discovery_request, envoy::config::core::v3alpha::ApiVersion::AUTO);
  EXPECT_EQ("{\"node\":{\"build_version\":\"foo\",\"user_agent_name\":\"bar\"}}",
            auto_discovery_request);
  const std::string v3_discovery_request = VersionConverter::getJsonStringFromMessage(
      discovery_request, envoy::config::core::v3alpha::ApiVersion::V3ALPHA);
  EXPECT_EQ("{\"node\":{\"user_agent_name\":\"bar\"}}", v3_discovery_request);
}

bool hasUnknownFields(const Protobuf::Message& message) {
  const Protobuf::Reflection* reflection = message.GetReflection();
  const auto& unknown_field_set = reflection->GetUnknownFields(message);
  return !unknown_field_set.empty();
}

// Validate that we can sensibly provide a gRPC wire interpretation of messages
// such as DiscoveryRequest based on transport API version.
TEST(VersionConverter, PrepareMessageForGrpcWire) {
  API_NO_BOOST(envoy::api::v2::core::Node) v2_node;
  v2_node.set_build_version("foo");
  v2_node.set_user_agent_name("bar");
  API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) discovery_request;
  discovery_request.mutable_node()->set_hidden_envoy_deprecated_build_version("foo");
  VersionConverter::upgrade(v2_node, *discovery_request.mutable_node());
  {
    API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) discovery_request_copy;
    discovery_request_copy.MergeFrom(discovery_request);
    VersionConverter::prepareMessageForGrpcWire(discovery_request_copy,
                                                envoy::config::core::v3alpha::ApiVersion::V2);
    API_NO_BOOST(envoy::api::v2::DiscoveryRequest) v2_discovery_request;
    EXPECT_TRUE(v2_discovery_request.ParseFromString(discovery_request_copy.SerializeAsString()));
    EXPECT_EQ("foo", v2_discovery_request.node().build_version());
    EXPECT_FALSE(hasUnknownFields(v2_discovery_request.node()));
  }
  {
    API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) discovery_request_copy;
    discovery_request_copy.MergeFrom(discovery_request);
    VersionConverter::prepareMessageForGrpcWire(discovery_request_copy,
                                                envoy::config::core::v3alpha::ApiVersion::AUTO);
    API_NO_BOOST(envoy::api::v2::DiscoveryRequest) auto_discovery_request;
    EXPECT_TRUE(auto_discovery_request.ParseFromString(discovery_request_copy.SerializeAsString()));
    EXPECT_EQ("foo", auto_discovery_request.node().build_version());
    EXPECT_FALSE(hasUnknownFields(auto_discovery_request.node()));
  }
  {
    API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) discovery_request_copy;
    discovery_request_copy.MergeFrom(discovery_request);
    VersionConverter::prepareMessageForGrpcWire(discovery_request_copy,
                                                envoy::config::core::v3alpha::ApiVersion::V3ALPHA);
    API_NO_BOOST(envoy::service::discovery::v3alpha::DiscoveryRequest) v3_discovery_request;
    EXPECT_TRUE(v3_discovery_request.ParseFromString(discovery_request_copy.SerializeAsString()));
    EXPECT_EQ("", v3_discovery_request.node().hidden_envoy_deprecated_build_version());
    EXPECT_FALSE(hasUnknownFields(v3_discovery_request.node()));
  }
}

// Downgrading to an earlier version (where it exists).
TEST(VersionConverterTest, DowngradeEarlier) {
  API_NO_BOOST(envoy::config::cluster::v3alpha::Cluster) source;
  source.set_ignore_health_on_host_removal(true);
  auto downgraded = VersionConverter::downgrade(source);
  const Protobuf::Descriptor* desc = downgraded->msg_->GetDescriptor();
  const Protobuf::Reflection* reflection = downgraded->msg_->GetReflection();
  EXPECT_EQ("envoy.api.v2.Cluster", desc->full_name());
  EXPECT_EQ(true, reflection->GetBool(*downgraded->msg_,
                                      desc->FindFieldByName("drain_connections_on_host_removal")));
}

// Downgrading is idempotent if no earlier version.
TEST(VersionConverterTest, DowngradeSame) {
  API_NO_BOOST(envoy::api::v2::Cluster) source;
  source.set_drain_connections_on_host_removal(true);
  auto downgraded = VersionConverter::downgrade(source);
  const Protobuf::Descriptor* desc = downgraded->msg_->GetDescriptor();
  const Protobuf::Reflection* reflection = downgraded->msg_->GetReflection();
  EXPECT_EQ("envoy.api.v2.Cluster", desc->full_name());
  EXPECT_EQ(true, reflection->GetBool(*downgraded->msg_,
                                      desc->FindFieldByName("drain_connections_on_host_removal")));
}

} // namespace
} // namespace Config
} // namespace Envoy
