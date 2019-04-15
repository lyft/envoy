#include <chrono>
#include <memory>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/logical_dns_cluster.h"

#include "source/extensions/clusters/redis/redis_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"

using testing::_;
using testing::ContainerEq;
using testing::DoAll;
using testing::Eq;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class RedisClusterTest : public testing::Test,
                         public Extensions::NetworkFilters::Common::Redis::Client::ClientFactory {
public:
  // ClientFactory
  Extensions::NetworkFilters::Common::Redis::Client::ClientPtr
  create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
         const Extensions::NetworkFilters::Common::Redis::Client::Config&) override {
    return Extensions::NetworkFilters::Common::Redis::Client::ClientPtr{create_()};
  }

  MOCK_METHOD0(create_, Extensions::NetworkFilters::Common::Redis::Client::Client*());

protected:
  RedisClusterTest() : api_(Api::createApiForTest(stats_store_)) {}

  std::list<std::string> hostListToAddresses(const Upstream::HostVector& hosts) {
    std::list<std::string> addresses;
    for (const Upstream::HostSharedPtr& host : hosts) {
      addresses.push_back(host->address()->asString());
    }

    return addresses;
  }

  void setupFromV2Yaml(const std::string& yaml) {
    expectRedisSessionCreated();
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, *api_);

    envoy::config::cluster::redis::RedisClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(), config);

    cluster_.reset(new RedisCluster(
        cluster_config,
        MessageUtil::downcastAndValidate<const envoy::config::cluster::redis::RedisClusterConfig&>(
            config),
        *this, cm, runtime_, dns_resolver_, factory_context, std::move(scope), false));
    // this allows us to create expectation on cluster slot response without waiting for makeRequest
    pool_callbacks_ = &*(cluster_->redis_discovery_session_);
    cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) -> void {
          membership_updated_.ready();
        });
  }

  void setupFactoryFromV2Yaml(const std::string& yaml) {
    NiceMock<Upstream::MockClusterManager> cm;
    envoy::api::v2::Cluster cluster_config = Upstream::parseClusterFromV2Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, *api_);

    envoy::config::cluster::redis::RedisClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufWkt::Struct::default_instance(), config);

    NiceMock<AccessLog::MockAccessLogManager> log_manager;
    NiceMock<Upstream::Outlier::EventLoggerSharedPtr> outlier_event_logger;
    NiceMock<Envoy::Api::MockApi> api;
    Upstream::ClusterFactoryContextImpl cluster_factory_context(
        cm, stats_store_, tls_, std::move(dns_resolver_), ssl_context_manager_, runtime_, random_,
        dispatcher_, log_manager, local_info_, admin_, singleton_manager_,
        std::move(outlier_event_logger), false, api);

    RedisClusterFactory factory = RedisClusterFactory();
    factory.createClusterWithConfig(cluster_config, config, cluster_factory_context,
                                    factory_context, std::move(scope));
  }

  void expectResolveDiscovery(Network::DnsLookupFamily dns_lookup_family,
                              const std::string& expected_address,
                              const std::list<std::string>& resolved_addresses) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, dns_lookup_family, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          cb(TestUtility::makeDnsResponse(resolved_addresses));
          return nullptr;
        }));
  }

  void expectRedisSessionCreated() { resolve_timer_ = new Event::MockTimer(&dispatcher_); }

  void expectRedisResolve() {
    client_ = new Extensions::NetworkFilters::Common::Redis::Client::MockClient();
    EXPECT_CALL(*this, create_()).WillOnce(Return(client_));
    EXPECT_CALL(*client_, addConnectionCallbacks(_));
    EXPECT_CALL(*client_, makeRequest(Ref(RedisCluster::ClusterSlotsRequest::instance_), _))
        .WillOnce(Return(&pool_request_));
  }

  void expectClusterSlotResponse(NetworkFilters::Common::Redis::RespValuePtr&& response) {
    EXPECT_CALL(*client_, close());
    EXPECT_CALL(*resolve_timer_, enableTimer(_));
    pool_callbacks_->onResponse(std::move(response));
  }

  void expectClusterSlotFailure() {
    EXPECT_CALL(*client_, close());
    EXPECT_CALL(*resolve_timer_, enableTimer(_));
    pool_callbacks_->onFailure();
  }

  NetworkFilters::Common::Redis::RespValuePtr
  singleSlotMasterSlave(const std::string& master, const std::string& slave, int64_t port) const {
    std::vector<NetworkFilters::Common::Redis::RespValue> master_1(2);
    master_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    master_1[0].asString() = master;
    master_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    master_1[1].asInteger() = port;

    std::vector<NetworkFilters::Common::Redis::RespValue> slave_1(2);
    slave_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    slave_1[0].asString() = slave;
    slave_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slave_1[1].asInteger() = port;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(4);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 16383;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(master_1);
    slot_1[3].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[3].asArray().swap(slave_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(1);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);

    NetworkFilters::Common::Redis::RespValuePtr response(
        new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);
    return response;
  }

  NetworkFilters::Common::Redis::RespValuePtr twoSlotsMasters() const {
    std::vector<NetworkFilters::Common::Redis::RespValue> master_1(2);
    master_1[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    master_1[0].asString() = "127.0.0.1";
    master_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    master_1[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> master_2(2);
    master_2[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
    master_2[0].asString() = "127.0.0.2";
    master_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    master_2[1].asInteger() = 22120;

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_1(3);
    slot_1[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[0].asInteger() = 0;
    slot_1[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_1[1].asInteger() = 9999;
    slot_1[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_1[2].asArray().swap(master_1);

    std::vector<NetworkFilters::Common::Redis::RespValue> slot_2(3);
    slot_2[0].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[0].asInteger() = 10000;
    slot_2[1].type(NetworkFilters::Common::Redis::RespType::Integer);
    slot_2[1].asInteger() = 16383;
    slot_2[2].type(NetworkFilters::Common::Redis::RespType::Array);
    slot_2[2].asArray().swap(master_2);

    std::vector<NetworkFilters::Common::Redis::RespValue> slots(2);
    slots[0].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[0].asArray().swap(slot_1);
    slots[1].type(NetworkFilters::Common::Redis::RespType::Array);
    slots[1].asArray().swap(slot_2);

    NetworkFilters::Common::Redis::RespValuePtr response(
        new NetworkFilters::Common::Redis::RespValue());
    response->type(NetworkFilters::Common::Redis::RespType::Array);
    response->asArray().swap(slots);
    return response;
  }

  void
  expectHealthyHosts(const std::list<std::string, std::allocator<std::string>>& healthy_hosts) {
    EXPECT_THAT(healthy_hosts, ContainerEq(hostListToAddresses(
                                   cluster_->prioritySet().hostSetsPerPriority()[0]->hosts())));
    EXPECT_THAT(healthy_hosts,
                ContainerEq(hostListToAddresses(
                    cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts())));
    EXPECT_EQ(1UL,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
    EXPECT_EQ(
        1UL,
        cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  }

  void testBasicSetup(const std::string& config, const std::string& expected_discovery_address) {

    setupFromV2Yaml(config);
    const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
    expectResolveDiscovery(Network::DnsLookupFamily::V4Only, expected_discovery_address,
                           resolved_addresses);
    expectRedisResolve();

    EXPECT_CALL(membership_updated_, ready());
    EXPECT_CALL(initialized_, ready());
    cluster_->initialize([&]() -> void { initialized_.ready(); });

    expectClusterSlotResponse(singleSlotMasterSlave("127.0.0.1", "127.0.0.2", 22120));
    // TODO(hyang): this will change once we register slaves as well
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));

    // add new host
    expectRedisResolve();
    EXPECT_CALL(membership_updated_, ready());
    resolve_timer_->callback_();
    expectClusterSlotResponse(twoSlotsMasters());
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

    // no change
    expectRedisResolve();
    resolve_timer_->callback_();
    expectClusterSlotResponse(twoSlotsMasters());
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120", "127.0.0.2:22120"}));

    // remove host
    expectRedisResolve();
    EXPECT_CALL(membership_updated_, ready());
    resolve_timer_->callback_();
    expectClusterSlotResponse(singleSlotMasterSlave("127.0.0.1", "127.0.0.2", 22120));
    expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::shared_ptr<RedisCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest().currentThreadId()};
  Api::ApiPtr api_;
  std::shared_ptr<Upstream::MockClusterMockPrioritySet> hosts_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* interval_timer_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockClient* client_{};
  Extensions::NetworkFilters::Common::Redis::Client::MockPoolRequest pool_request_;
  Extensions::NetworkFilters::Common::Redis::Client::PoolCallbacks* pool_callbacks_{};
};

typedef std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>,
                   std::list<std::string>>
    RedisDnsConfigTuple;
std::vector<RedisDnsConfigTuple> generateRedisDnsParams() {
  std::vector<RedisDnsConfigTuple> dns_config;
  {
    std::string family_yaml("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    std::list<std::string> resolved_host{"127.0.0.1:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: V4_ONLY)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    std::list<std::string> resolved_host{"127.0.0.1:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: V6_ONLY)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "::2"};
    std::list<std::string> resolved_host{"[::1]:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: AUTO)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"::1", "::2"};
    std::list<std::string> resolved_host{"[::1]:22120"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response, resolved_host));
  }
  return dns_config;
}

class RedisDnsParamTest : public RedisClusterTest,
                          public testing::WithParamInterface<RedisDnsConfigTuple> {};

INSTANTIATE_TEST_SUITE_P(DnsParam, RedisDnsParamTest, testing::ValuesIn(generateRedisDnsParams()));

// Validate that if the DNS and CLUSTER SLOT resolve immediately, we have the expected
// host state and initialization callback invocation
TEST_P(RedisDnsParamTest, ImmediateResolveDns) {
  const std::string config = R"EOF(
  name: name
  connect_timeout: 0.25s
  )EOF" + std::get<0>(GetParam()) +
                             R"EOF(
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  setupFromV2Yaml(config);

  expectRedisResolve();
  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        std::list<std::string> address_pair = std::get<2>(GetParam());
        cb(TestUtility::makeDnsResponse(address_pair));
        expectClusterSlotResponse(
            singleSlotMasterSlave(address_pair.front(), address_pair.back(), 22120));
        return nullptr;
      }));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  cluster_->initialize([&]() -> void { initialized_.ready(); });

  expectHealthyHosts(std::get<3>(GetParam()));
}

TEST_F(RedisClusterTest, Basic) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  // using load assignment
  const std::string basic_yaml_load_assignment = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.bar.com
                port_value: 22120
            health_check_config:
              port_value: 8000
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  testBasicSetup(basic_yaml_hosts, "foo.bar.com");
  testBasicSetup(basic_yaml_load_assignment, "foo.bar.com");
}

TEST_F(RedisClusterTest, RedisResolveFailure) {

  const std::string basic_yaml_hosts = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";
  setupFromV2Yaml(basic_yaml_hosts);
  const std::list<std::string> resolved_addresses{"127.0.0.1", "127.0.0.2"};
  expectResolveDiscovery(Network::DnsLookupFamily::V4Only, "foo.bar.com", resolved_addresses);
  expectRedisResolve();

  cluster_->initialize([&]() -> void { initialized_.ready(); });

  // initialization will wait til the redis cluster succeed
  expectClusterSlotFailure();
  EXPECT_EQ(1U, cluster_->info()->stats().update_attempt_.value());
  EXPECT_EQ(1U, cluster_->info()->stats().update_failure_.value());

  expectRedisResolve();
  resolve_timer_->callback_();
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  expectClusterSlotResponse(singleSlotMasterSlave("127.0.0.1", "127.0.0.2", 22120));
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));

  // expect no change if resolve failed
  expectRedisResolve();
  resolve_timer_->callback_();
  expectClusterSlotFailure();
  expectHealthyHosts(std::list<std::string>({"127.0.0.1:22120"}));
  EXPECT_EQ(3U, cluster_->info()->stats().update_attempt_.value());
  EXPECT_EQ(2U, cluster_->info()->stats().update_failure_.value());
}

TEST_F(RedisClusterTest, FactoryInitNotRedisClusterTypeFailure) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.memcached
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFactoryFromV2Yaml(basic_yaml_hosts), EnvoyException,
                            "Redis cluster can only created with redis cluster type");
}

TEST_F(RedisClusterTest, FactoryInitRedisClusterTypeSuccess) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  connect_timeout: 0.25s
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 22120
  cluster_type:
    name: envoy.clusters.redis
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        cluster_refresh_rate: 4s
        cluster_refresh_timeout: 0.25s
  )EOF";
  setupFactoryFromV2Yaml(basic_yaml_hosts);
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
