#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/kafka/mesh/upstream_config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

TEST(UpstreamKafkaConfigurationTest, shouldThrowIfNoKafkaClusters) {
  // given
  KafkaMeshProtoConfig proto_config;

  // when
  // then - exception gets thrown
  EXPECT_THROW_WITH_REGEX(UpstreamKafkaConfigurationImpl{proto_config}, EnvoyException,
                          "at least one upstream Kafka cluster");
}

TEST(UpstreamKafkaConfigurationTest, shouldThrowIfKafkaClustersWithSameName) {
  // given
  const std::string yaml = R"EOF(
advertised_host: mock
advertised_port: 1
upstream_clusters:
- cluster_name: REPEATEDNAME
  bootstrap_servers: mock
  partition_count : 1
- cluster_name: REPEATEDNAME
  bootstrap_servers: mock
  partition_count : 1
forwarding_rules:
  )EOF";
  KafkaMeshProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // when
  // then - exception gets thrown
  EXPECT_THROW_WITH_REGEX(UpstreamKafkaConfigurationImpl{proto_config}, EnvoyException,
                          "multiple Kafka clusters referenced by the same name");
}

TEST(UpstreamKafkaConfigurationTest, shouldThrowIfNoForwardingRules) {
  // given
  const std::string yaml = R"EOF(
advertised_host: mock_host
advertised_port: 42
upstream_clusters:
- cluster_name: mock
  bootstrap_servers: mock
  partition_count : 1
forwarding_rules:
  )EOF";
  KafkaMeshProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // when
  // then - exception gets thrown
  EXPECT_THROW_WITH_REGEX(UpstreamKafkaConfigurationImpl{proto_config}, EnvoyException,
                          "at least one forwarding rule");
}

TEST(UpstreamKafkaConfigurationTest, shouldThrowIfForwardingRuleWithUnknownTarget) {
  // given
  const std::string yaml = R"EOF(
advertised_host: mock_host
advertised_port: 42
upstream_clusters:
- cluster_name: mock
  bootstrap_servers: mock
  partition_count : 1
forwarding_rules:
- target_cluster: BADNAME
  topic_prefix: mock
  )EOF";
  KafkaMeshProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  // when
  // then - exception gets thrown
  EXPECT_THROW_WITH_REGEX(UpstreamKafkaConfigurationImpl{proto_config}, EnvoyException,
                          "forwarding rule is referencing unknown upstream Kafka cluster");
}

TEST(UpstreamKafkaConfigurationTest, shouldBehaveProperly) {
  // given
  const std::string yaml = R"EOF(
advertised_host: mock_host
advertised_port: 42
upstream_clusters:
- cluster_name: cluster1
  bootstrap_servers: s1
  partition_count : 1
- cluster_name: cluster2
  bootstrap_servers: s2
  partition_count : 2
forwarding_rules:
- target_cluster: cluster1
  topic_prefix: prefix1
- target_cluster: cluster2
  topic_prefix: prefix2
  )EOF";
  KafkaMeshProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  const UpstreamKafkaConfiguration& testee = UpstreamKafkaConfigurationImpl{proto_config};

  const ClusterConfig cluster1 = {"cluster1", 1, {{"bootstrap.servers", "s1"}}};
  const ClusterConfig cluster2 = {"cluster2", 2, {{"bootstrap.servers", "s2"}}};

  // when, then (advertised address is returned properly)
  const auto address = testee.getAdvertisedAddress();
  EXPECT_EQ(address.first, "mock_host");
  EXPECT_EQ(address.second, 42);

  // when, then (matching prefix with something more)
  const auto res1 = testee.computeClusterConfigForTopic("prefix1somethingmore");
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(*res1, cluster1);

  // when, then (matching prefix alone)
  const auto res2 = testee.computeClusterConfigForTopic("prefix1");
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(*res2, cluster1);

  // when, then (failing to match first rule, but then matching the second one)
  const auto res3 = testee.computeClusterConfigForTopic("prefix2somethingmore");
  ASSERT_TRUE(res3.has_value());
  EXPECT_EQ(*res3, cluster2);

  // when, then (no rules match)
  const auto res4 = testee.computeClusterConfigForTopic("someotherthing");
  EXPECT_FALSE(res4.has_value());
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
