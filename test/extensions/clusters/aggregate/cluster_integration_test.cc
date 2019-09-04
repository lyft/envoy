#include "common/singleton/manager_impl.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "extensions/clusters/aggregate/cluster.h"

#include "test/common/upstream/test_cluster_manager.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

using testing::AtLeast;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::SizeIs;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

envoy::config::bootstrap::v2::Bootstrap parseBootstrapFromV2Yaml(const std::string& yaml) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  TestUtility::loadFromYaml(yaml, bootstrap);
  return bootstrap;
}

class AggregateClusterIntegrationTest : public testing::Test {
public:
  AggregateClusterIntegrationTest() : http_context_(stats_store_.symbolTable()) {}

  void initialize(const std::string& yaml_config) {
    cluster_manager_ = std::make_unique<Upstream::TestClusterManagerImpl>(
        parseBootstrapFromV2Yaml(yaml_config), factory_, factory_.stats_, factory_.tls_,
        factory_.runtime_, factory_.random_, factory_.local_info_, log_manager_,
        factory_.dispatcher_, admin_, validation_context_, *api_, http_context_);

    EXPECT_EQ(cluster_manager_->activeClusters().size(), 1);
    cluster_ = cluster_manager_->get("aggregate_cluster");
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Server::MockAdmin> admin_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  Upstream::ThreadLocalCluster* cluster_;

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Upstream::TestClusterManagerFactory> factory_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::unique_ptr<Upstream::TestClusterManagerImpl> cluster_manager_;
  AccessLog::MockAccessLogManager log_manager_;
  Http::ContextImpl http_context_;

  const std::string default_yaml_config_ = R"EOF(
 static_resources:
  clusters:
  - name: aggregate_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.aggregate
      typed_config:
        "@type": type.googleapis.com/envoy.config.cluster.aggregate.ClusterConfig
        clusters:
        - primary
        - secondary
  )EOF";
};

TEST_F(AggregateClusterIntegrationTest, NoHealthyUpstream) {
  initialize(default_yaml_config_);
  EXPECT_EQ(nullptr, cluster_->loadBalancer().chooseHost(nullptr));
}

TEST_F(AggregateClusterIntegrationTest, BasicFlow) {
  initialize(default_yaml_config_);

  std::unique_ptr<Upstream::MockClusterUpdateCallbacks> callbacks(
      new NiceMock<Upstream::MockClusterUpdateCallbacks>());
  Upstream::ClusterUpdateCallbacksHandlePtr cb =
      cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  auto primary = cluster_manager_->get("primary");
  EXPECT_NE(nullptr, primary);
  auto host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(
      cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("secondary"), ""));
  auto secondary = cluster_manager_->get("secondary");
  EXPECT_NE(nullptr, secondary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("tertiary"), ""));
  auto tertiary = cluster_manager_->get("tertiary");
  EXPECT_NE(nullptr, tertiary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  EXPECT_TRUE(cluster_manager_->removeCluster("primary"));
  EXPECT_EQ(nullptr, cluster_manager_->get("primary"));
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("secondary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());
  EXPECT_EQ(3, cluster_manager_->activeClusters().size());

  EXPECT_TRUE(cluster_manager_->addOrUpdateCluster(Upstream::defaultStaticCluster("primary"), ""));
  primary = cluster_manager_->get("primary");
  EXPECT_NE(nullptr, primary);
  host = cluster_->loadBalancer().chooseHost(nullptr);
  EXPECT_NE(nullptr, host);
  EXPECT_EQ("primary", host->cluster().name());
  EXPECT_EQ("127.0.0.1:11001", host->address()->asString());

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host1 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.1:80");
  host1->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host2 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.2:80");
  host2->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host3 = Upstream::makeTestHost(primary->info(), "tcp://127.0.0.3:80");
  Upstream::HostVector hosts{host1, host2, host3};
  auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

  Upstream::Cluster& cluster = cluster_manager_->activeClusters().find("primary")->second;
  cluster.prioritySet().updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()),
      nullptr, hosts, {}, 100);

  // Set up the HostSet with 1 healthy, 1 degraded and 1 unhealthy.
  Upstream::HostSharedPtr host4 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.4:80");
  host4->healthFlagSet(Upstream::HostImpl::HealthFlag::DEGRADED_ACTIVE_HC);
  Upstream::HostSharedPtr host5 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.5:80");
  host5->healthFlagSet(Upstream::HostImpl::HealthFlag::FAILED_ACTIVE_HC);
  Upstream::HostSharedPtr host6 = Upstream::makeTestHost(secondary->info(), "tcp://127.0.0.6:80");
  Upstream::HostVector hosts1{host4, host5, host6};
  auto hosts_ptr1 = std::make_shared<Upstream::HostVector>(hosts1);
  Upstream::Cluster& cluster1 = cluster_manager_->activeClusters().find("secondary")->second;
  cluster1.prioritySet().updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(hosts_ptr1, Upstream::HostsPerLocalityImpl::empty()),
      nullptr, hosts1, {}, 100);

  for (int i = 0; i < 33; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("primary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Healthy, host->health());
  }

  for (int i = 33; i < 66; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("secondary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Healthy, host->health());
  }

  for (int i = 66; i < 99; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("primary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Degraded, host->health());
  }

  for (int i = 99; i < 100; ++i) {
    EXPECT_CALL(factory_.random_, random()).WillRepeatedly(Return(i));
    host = cluster_->loadBalancer().chooseHost(nullptr);
    EXPECT_EQ("secondary", host->cluster().name());
    EXPECT_EQ(Upstream::Host::Health::Degraded, host->health());
  }
}

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy