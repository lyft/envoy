#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/router/router.h"

#include "common/network/utility.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
};

class RingHashLoadBalancerTest : public TestBaseWithParam<bool> {
public:
  RingHashLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init() {
    lb_ = std::make_unique<RingHashLoadBalancer>(priority_set_, stats_, stats_store_, runtime_,
                                                 random_, config_, common_config_);
    lb_->initialize();
  }

  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functonality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<RingHashLoadBalancer> lb_;
};

// For tests which don't need to be run in both primary and failover modes.
typedef RingHashLoadBalancerTest RingHashFailoverTest;

INSTANTIATE_TEST_SUITE_P(RingHashPrimaryOrFailover, RingHashLoadBalancerTest,
                         ::testing::Values(true, false));
INSTANTIATE_TEST_SUITE_P(RingHashPrimaryOrFailover, RingHashFailoverTest, ::testing::Values(true));

TEST_P(RingHashLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

TEST_P(RingHashLoadBalancerTest, BadRingSizeBounds) {
  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(20);
  config_.value().mutable_maximum_ring_size()->set_value(10);
  EXPECT_THROW_WITH_MESSAGE(init(), EnvoyException,
                            "ring hash: minimum_ring_size (20) > maximum_ring_size (10)");
}

TEST_P(RingHashLoadBalancerTest, Basic) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);

  init();
  EXPECT_EQ("ring_hash_lb.size", lb_->stats().size_.name());
  EXPECT_EQ("ring_hash_lb.replication_factor", lb_->stats().replication_factor_.name());
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().replication_factor_.value());

  // hash ring:
  // port | position
  // ---------------------------
  // :94  | 833437586790550860
  // :92  | 928266305478181108
  // :90  | 1033482794131418490
  // :95  | 3551244743356806947
  // :93  | 3851675632748031481
  // :91  | 5583722120771150861
  // :91  | 6311230543546372928
  // :93  | 7700377290971790572
  // :95  | 13144177310400110813
  // :92  | 13444792449719432967
  // :94  | 15516499411664133160
  // :90  | 16117243373044804889

  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806947);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806948);
    EXPECT_EQ(hostSet().hosts_[3], lb->chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(16117243373044804880UL));
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());

  hostSet().healthy_hosts_.clear();
  hostSet().runCallbacks({}, {});
  lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    if (GetParam() == 1) {
      EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
    } else {
      // When all hosts are unhealthy, the default behavior of the load balancer is to send
      // traffic to P=0. In this case, P=0 has no backends so it returns nullptr.
      EXPECT_EQ(nullptr, lb->chooseHost(&context));
    }
  }
  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts are used.
TEST_P(RingHashFailoverTest, BasicFailover) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80")};
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82")};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(12, lb_->stats().replication_factor_.value());

  LoadBalancerPtr lb = lb_->factory()->create();
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Add a healthy host at P=0 and it will be chosen.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to the failover_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Set up so P=0 gets 70% of the load, and P=1 gets 30%.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  host_set_.healthy_hosts_ = {host_set_.hosts_[0]};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_CALL(random_, random()).WillOnce(Return(69));
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
  EXPECT_CALL(random_, random()).WillOnce(Return(71));
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
}

#if __GLIBCXX__ >= 20130411 && __GLIBCXX__ <= 20180726
// Run similar tests with the default hash algorithm for GCC 5.
// TODO(danielhochman): After v1 is deprecated this test can be deleted since std::hash will no
// longer be in use.
TEST_P(RingHashLoadBalancerTest, BasicWithStdHash) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"), makeTestHost(info_, "tcp://127.0.0.1:85")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(true);
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().replication_factor_.value());

  // This is the hash ring built using the default hash (probably murmur2) on GCC 5.4.
  // ring hash: host=127.0.0.1:85 hash=1358027074129602068
  // ring hash: host=127.0.0.1:83 hash=4361834613929391114
  // ring hash: host=127.0.0.1:84 hash=7224494972555149682
  // ring hash: host=127.0.0.1:81 hash=7701421856454313576
  // ring hash: host=127.0.0.1:82 hash=8649315368077433379
  // ring hash: host=127.0.0.1:84 hash=8739448859063030639
  // ring hash: host=127.0.0.1:81 hash=9887544217113020895
  // ring hash: host=127.0.0.1:82 hash=10150910876324007731
  // ring hash: host=127.0.0.1:83 hash=15168472011420622455
  // ring hash: host=127.0.0.1:80 hash=15427156902705414897
  // ring hash: host=127.0.0.1:85 hash=16375050414328759093
  // ring hash: host=127.0.0.1:80 hash=17613279263364193813
  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602068);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602069);
    EXPECT_EQ(hostSet().hosts_[3], lb->chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(10150910876324007730UL));
    EXPECT_EQ(hostSet().hosts_[2], lb->chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}
#endif

TEST_P(RingHashLoadBalancerTest, BasicWithMurmur2) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"), makeTestHost(info_, "tcp://127.0.0.1:85")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().set_hash_function(envoy::api::v2::Cluster_RingHashLbConfig_HashFunction::
                                        Cluster_RingHashLbConfig_HashFunction_MURMUR_HASH_2);
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().replication_factor_.value());

  // This is the hash ring built using murmur2 hash.
  // ring hash: host=127.0.0.1:85 hash=1358027074129602068
  // ring hash: host=127.0.0.1:83 hash=4361834613929391114
  // ring hash: host=127.0.0.1:84 hash=7224494972555149682
  // ring hash: host=127.0.0.1:81 hash=7701421856454313576
  // ring hash: host=127.0.0.1:82 hash=8649315368077433379
  // ring hash: host=127.0.0.1:84 hash=8739448859063030639
  // ring hash: host=127.0.0.1:81 hash=9887544217113020895
  // ring hash: host=127.0.0.1:82 hash=10150910876324007731
  // ring hash: host=127.0.0.1:83 hash=15168472011420622455
  // ring hash: host=127.0.0.1:80 hash=15427156902705414897
  // ring hash: host=127.0.0.1:85 hash=16375050414328759093
  // ring hash: host=127.0.0.1:80 hash=17613279263364193813
  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602068);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602069);
    EXPECT_EQ(hostSet().hosts_[3], lb->chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(10150910876324007730UL));
    EXPECT_EQ(hostSet().hosts_[2], lb->chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}

TEST_P(RingHashLoadBalancerTest, UnevenHosts) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(3);
  init();
  EXPECT_EQ(4, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().replication_factor_.value());

  // hash ring:
  // port | position
  // ---------------------------
  // :80  | 5454692015285649509
  // :81  | 7859399908942313493
  // :80  | 13838424394637650569
  // :81  | 16064866803292627174

  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }

  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81"),
                      makeTestHost(info_, "tcp://127.0.0.1:82")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  // hash ring:
  // port | position
  // ------------------
  // :81  | 7859399908942313493
  // :82  | 8241336090459785962
  // :82  | 12882406409176325258
  // :81  | 16064866803292627174

  lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }
}

TEST_P(RingHashLoadBalancerTest, HostWeightedTinyRing) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly six entries
  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(6);
  config_.value().mutable_maximum_ring_size()->set_value(6);
  init();
  EXPECT_EQ(6, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 should appear twice and :92 should appear three times.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},  {4443673547860492590UL, 2},  {5583722120771150861UL, 1},
      {6311230543546372928UL, 1}, {13444792449719432967UL, 2}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

TEST_P(RingHashLoadBalancerTest, HostWeightedLargeRing) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_replication_factor()->set_value(1024);
  init();
  EXPECT_EQ(6144, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 6000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[3] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(987, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(1932, counts[1]); // :91 | ~2000 expected hits
  EXPECT_EQ(3081, counts[2]); // :92 | ~3000 expected hits
}

TEST_P(RingHashLoadBalancerTest, LocalityWeightedTinyRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly six entries
  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(6);
  config_.value().mutable_maximum_ring_size()->set_value(6);
  init();
  EXPECT_EQ(6, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 should appear twice, :92 should appear three times,
  // and :93 shouldn't appear at all.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},  {4443673547860492590UL, 2},  {5583722120771150861UL, 1},
      {6311230543546372928UL, 1}, {13444792449719432967UL, 2}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

TEST_P(RingHashLoadBalancerTest, LocalityWeightedLargeRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_replication_factor()->set_value(1024);
  init();
  EXPECT_EQ(6144, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 6000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(987, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(1932, counts[1]); // :91 | ~2000 expected hits
  EXPECT_EQ(3081, counts[2]); // :92 | ~3000 expected hits
  EXPECT_EQ(0, counts[3]);    // :93 |    =0 expected hits
}

TEST_P(RingHashLoadBalancerTest, HostAndLocalityWeightedSmallRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90", 1), makeTestHost(info_, "tcp://127.0.0.1:91", 2),
      makeTestHost(info_, "tcp://127.0.0.1:92", 3), makeTestHost(info_, "tcp://127.0.0.1:93", 4)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly 14 entries
  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(14);
  config_.value().mutable_maximum_ring_size()->set_value(14);
  init();
  EXPECT_EQ(14, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 should appear four times, :92 should appear nine times,
  // and :93 shouldn't appear at all.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},   {4443673547860492590UL, 2},  {4470782202023056897UL, 1},
      {5583722120771150861UL, 1},  {6311230543546372928UL, 1},  {7028796200958575341UL, 2},
      {7622568113965459810UL, 2},  {8301579928699792521UL, 1},  {8763220459450311387UL, 2},
      {13444792449719432967UL, 2}, {14054452251593525090UL, 2}, {15052576707013241299UL, 2},
      {15299362238897758650UL, 2}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

TEST_P(RingHashLoadBalancerTest, HostAndLocalityWeightedLargeRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90", 1), makeTestHost(info_, "tcp://127.0.0.1:91", 2),
      makeTestHost(info_, "tcp://127.0.0.1:92", 3), makeTestHost(info_, "tcp://127.0.0.1:93", 4)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_replication_factor()->set_value(1024);
  init();
  EXPECT_EQ(14336, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 14000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 14000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 14000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(980, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(3928, counts[1]); // :91 | ~4000 expected hits
  EXPECT_EQ(9092, counts[2]); // :92 | ~9000 expected hits
  EXPECT_EQ(0, counts[3]);    // :93 |    =0 expected hits
}

TEST_P(RingHashLoadBalancerTest, OverconstrainedRingSizeBounds) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = (envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(1024);
  config_.value().mutable_maximum_ring_size()->set_value(1024);
  init();
  EXPECT_EQ(1025, lb_->stats().size_.value()); // next highest multiple of 5 hosts
  EXPECT_EQ(205, lb_->stats().replication_factor_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 5000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[5] = {0};
  for (uint32_t i = 0; i < 5000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 5000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(1008, counts[0]); // :90 | ~1000 expected hits
  EXPECT_EQ(952, counts[1]);  // :91 | ~1000 expected hits
  EXPECT_EQ(984, counts[2]);  // :92 | ~1000 expected hits
  EXPECT_EQ(1022, counts[3]); // :93 | ~1000 expected hits
  EXPECT_EQ(1034, counts[4]); // :94 | ~1000 expected hits
}

} // namespace Upstream
} // namespace Envoy
