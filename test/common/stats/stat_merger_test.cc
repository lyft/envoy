#include <memory>

#include "common/stats/isolated_store_impl.h"
#include "common/stats/stat_merger.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatMergerTest : public testing::Test {
public:
  StatMergerTest() : stat_merger_(store_) { store_.gauge("whywassixafraidofseven").set(678); }

  Stats::IsolatedStoreImpl store_;
  StatMerger stat_merger_;
  Protobuf::Map<std::string, uint64_t> empty_counter_values_;
  Protobuf::Map<std::string, uint64_t> empty_counter_deltas_;
  Protobuf::Map<std::string, uint64_t> empty_gauges_;
};

TEST_F(StatMergerTest, counterMerge) {
  // Child's value of the counter might already be non-zero by the first merge.
  store_.counter("draculaer").inc();
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  Protobuf::Map<std::string, uint64_t> counter_values;
  Protobuf::Map<std::string, uint64_t> counter_deltas;
  counter_values["draculaer"] = 1;
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(counter_values, counter_deltas, empty_gauges_);
  // Initial combined value: 1+1+1.
  EXPECT_EQ(3, store_.counter("draculaer").value());
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  // The parent's counter increases by 1.
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(empty_counter_values_, counter_deltas, empty_gauges_);
  EXPECT_EQ(4, store_.counter("draculaer").value());
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.counter("draculaer").add(4);
  counter_deltas["draculaer"] = 0;
  stat_merger_.mergeStats(empty_counter_values_, counter_deltas, empty_gauges_);
  EXPECT_EQ(8, store_.counter("draculaer").value());
  EXPECT_EQ(4, store_.counter("draculaer").latch());

  // Our counter and the parent's counter both increase by 2, total increase of 4.
  store_.counter("draculaer").add(2);
  counter_deltas["draculaer"] = 2;
  stat_merger_.mergeStats(empty_counter_values_, counter_deltas, empty_gauges_);
  EXPECT_EQ(12, store_.counter("draculaer").value());
  EXPECT_EQ(4, store_.counter("draculaer").latch());
}

// It should be fine for the parent to send us stats we haven't ourselves instantiated.
TEST_F(StatMergerTest, newStatFromParent) {
  Protobuf::Map<std::string, uint64_t> counter_values;
  Protobuf::Map<std::string, uint64_t> counter_deltas;
  Protobuf::Map<std::string, uint64_t> gauges;
  counter_values["newcounter"] = 1;
  counter_deltas["newcounter"] = 0;
  gauges["newgauge"] = 5;
  stat_merger_.mergeStats(counter_values, counter_deltas, gauges);
  EXPECT_EQ(1, store_.counter("newcounter").value());
  EXPECT_EQ(0, store_.counter("newcounter").latch());
  EXPECT_EQ(5, store_.gauge("newgauge").value());
}

TEST_F(StatMergerTest, basicDefaultAccumulationImport) {
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["whywassixafraidofseven"] = 111;
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
}

TEST_F(StatMergerTest, multipleImportsWithAccumulationLogic) {
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["whywassixafraidofseven"] = 100;
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  // Initial combined values: 678+100 and 1+2.
  EXPECT_EQ(778, store_.gauge("whywassixafraidofseven").value());

  // The parent's gauge drops by 1, and its counter increases by 1.
  gauges["whywassixafraidofseven"] = 99;
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(777, store_.gauge("whywassixafraidofseven").value());

  // Our own gauge increases by 12, while the parent's stays constant. Total increase of 12.
  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.gauge("whywassixafraidofseven").add(12);
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());

  // Our gauge decreases by 5, parent's increases by 5. Net zero change.
  // Our counter and the parent's counter both increase by 1, total increase of 2.
  store_.gauge("whywassixafraidofseven").sub(5);
  gauges["whywassixafraidofseven"] = 104;
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
}

// Stat names that have NoImport logic should leave the child gauge value alone upon import, even if
// the child has that gauge undefined.
TEST_F(StatMergerTest, exclusionsNotImported) {
  store_.gauge("some.sort.of.version").set(12345);

  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["some.sort.of.version"] = 67890;
  gauges["child.doesnt.have.this.version"] = 111;

  // Check defined values are not changed, and undefined remain undefined.
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(12345, store_.gauge("some.sort.of.version").value());
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());

  // Check the "undefined remains undefined" behavior for a bunch of other names.
  gauges["runtime.admin_overrides_active"] = 111;
  gauges["runtime.num_keys"] = 111;
  gauges["listener_manager.total_listeners_draining"] = 111;
  gauges["server.hot_restart_epoch"] = 111;
  gauges["server.live"] = 1;
  gauges["some.control_plane.connected_state"] = 1;

  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());
  EXPECT_FALSE(store_.gauge("runtime.admin_overrides_active").used());
  EXPECT_FALSE(store_.gauge("runtime.num_keys").used());
  EXPECT_FALSE(store_.gauge("listener_manager.total_listeners_draining").used());
  EXPECT_FALSE(store_.gauge("listener_manager.total_listeners_warming").used());
  EXPECT_FALSE(store_.gauge("server.hot_restart_epoch").used());
  EXPECT_FALSE(store_.gauge("server.live").used());
  EXPECT_FALSE(store_.gauge("server.concurrency").used());
  EXPECT_FALSE(store_.gauge("some.connected_state").used());
}

// The OnlyImportWhenUnusedInChild logic should overwrite an undefined gauge, but not a defined one.
TEST_F(StatMergerTest, onlyImportWhenUnused) {
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["cluster_manager.active_clusters"] = 33;
  gauges["cluster_manager.warming_clusters"] = 33;
  gauges["cluster.rds.membership_total"] = 33;
  gauges["cluster.rds.membership_healthy"] = 33;
  gauges["cluster.rds.membership_degraded"] = 33;
  gauges["cluster.rds.max_host_weight"] = 33;
  gauges["anything.total_principals"] = 33;
  gauges["listener_manager.total_listeners_active"] = 33;
  gauges["some_sort_of_pressure"] = 33;
  // 33 is stored into the child's until-now-undefined gauges
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(33, store_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(33, store_.gauge("cluster_manager.warming_clusters").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_total").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_healthy").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_degraded").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.max_host_weight").value());
  EXPECT_EQ(33, store_.gauge("anything.total_principals").value());
  EXPECT_EQ(33, store_.gauge("listener_manager.total_listeners_active").value());
  EXPECT_EQ(33, store_.gauge("some_sort_of_pressure").value());
  store_.gauge("cluster_manager.active_clusters").set(88);
  store_.gauge("cluster_manager.warming_clusters").set(88);
  store_.gauge("cluster.rds.membership_total").set(88);
  store_.gauge("cluster.rds.membership_healthy").set(88);
  store_.gauge("cluster.rds.membership_degraded").set(88);
  store_.gauge("cluster.rds.max_host_weight").set(88);
  store_.gauge("anything.total_principals").set(88);
  store_.gauge("listener_manager.total_listeners_active").set(88);
  store_.gauge("some_sort_of_pressure").set(88);
  // Now that the child's gauges have been set to 88, merging the "33" values will make no change.
  stat_merger_.mergeStats(empty_counter_values_, empty_counter_deltas_, gauges);
  EXPECT_EQ(88, store_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(88, store_.gauge("cluster_manager.warming_clusters").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_total").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_healthy").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_degraded").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.max_host_weight").value());
  EXPECT_EQ(88, store_.gauge("anything.total_principals").value());
  EXPECT_EQ(88, store_.gauge("listener_manager.total_listeners_active").value());
  EXPECT_EQ(88, store_.gauge("some_sort_of_pressure").value());
}

} // namespace
} // namespace Stats
} // namespace Envoy
