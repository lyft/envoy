#include "test/common/stats/stat_test_utility.h"

#include "common/common/assert.h"
#include "common/memory/stats.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

void forEachSampleStat(int num_clusters, std::function<void(absl::string_view)> fn) {
  // These are stats that are repeated for each cluster as of Oct 2018, with a
  // very basic configuration with no traffic.
  static const char* cluster_stats[] = {"bind_errors",
                                        "lb_healthy_panic",
                                        "lb_local_cluster_not_ok",
                                        "lb_recalculate_zone_structures",
                                        "lb_subsets_active",
                                        "lb_subsets_created",
                                        "lb_subsets_fallback",
                                        "lb_subsets_removed",
                                        "lb_subsets_selected",
                                        "lb_zone_cluster_too_small",
                                        "lb_zone_no_capacity_left",
                                        "lb_zone_number_differs",
                                        "lb_zone_routing_all_directly",
                                        "lb_zone_routing_cross_zone",
                                        "lb_zone_routing_sampled",
                                        "max_host_weight",
                                        "membership_change",
                                        "membership_healthy",
                                        "membership_total",
                                        "original_dst_host_invalid",
                                        "retry_or_shadow_abandoned",
                                        "update_attempt",
                                        "update_empty",
                                        "update_failure",
                                        "update_no_rebuild",
                                        "update_success",
                                        "upstream_cx_active",
                                        "upstream_cx_close_notify",
                                        "upstream_cx_connect_attempts_exceeded",
                                        "upstream_cx_connect_fail",
                                        "upstream_cx_connect_timeout",
                                        "upstream_cx_destroy",
                                        "upstream_cx_destroy_local",
                                        "upstream_cx_destroy_local_with_active_rq",
                                        "upstream_cx_destroy_remote",
                                        "upstream_cx_destroy_remote_with_active_rq",
                                        "upstream_cx_destroy_with_active_rq",
                                        "upstream_cx_http1_total",
                                        "upstream_cx_http2_total",
                                        "upstream_cx_idle_timeout",
                                        "upstream_cx_max_requests",
                                        "upstream_cx_none_healthy",
                                        "upstream_cx_overflow",
                                        "upstream_cx_protocol_error",
                                        "upstream_cx_rx_bytes_buffered",
                                        "upstream_cx_rx_bytes_total",
                                        "upstream_cx_total",
                                        "upstream_cx_tx_bytes_buffered",
                                        "upstream_cx_tx_bytes_total",
                                        "upstream_flow_control_backed_up_total",
                                        "upstream_flow_control_drained_total",
                                        "upstream_flow_control_paused_reading_total",
                                        "upstream_flow_control_resumed_reading_total",
                                        "upstream_rq_active",
                                        "upstream_rq_cancelled",
                                        "upstream_rq_completed",
                                        "upstream_rq_maintenance_mode",
                                        "upstream_rq_pending_active",
                                        "upstream_rq_pending_failure_eject",
                                        "upstream_rq_pending_overflow",
                                        "upstream_rq_pending_total",
                                        "upstream_rq_per_try_timeout",
                                        "upstream_rq_retry",
                                        "upstream_rq_retry_overflow",
                                        "upstream_rq_retry_success",
                                        "upstream_rq_rx_reset",
                                        "upstream_rq_timeout",
                                        "upstream_rq_total",
                                        "upstream_rq_tx_reset",
                                        "version"};

  // These are the other stats that appear in the admin /stats request when made
  // prior to any requests.
  static const char* other_stats[] = {"http.admin.downstream_cx_length_ms",
                                      "http.admin.downstream_rq_time",
                                      "http.ingress_http.downstream_cx_length_ms",
                                      "http.ingress_http.downstream_rq_time",
                                      "listener.0.0.0.0_40000.downstream_cx_length_ms",
                                      "listener.admin.downstream_cx_length_ms"};

  for (int cluster = 0; cluster <= num_clusters; ++cluster) {
    for (const auto& cluster_stat : cluster_stats) {
      fn(absl::StrCat("cluster.service_", cluster, ".", cluster_stat));
    }
  }
  for (const auto& other_stat : other_stats) {
    fn(other_stat);
  }
}

MemoryTest::Mode MemoryTest::mode() {
#if !defined(TCMALLOC) || defined(ENVOY_MEMORY_DEBUG_ENABLED)
  // We can only test absolute memory usage if the malloc library is a known
  // quantity. This decision is centralized here. As the preferred malloc
  // library for Envoy is TCMALLOC that's what we test for here. If we switch
  // to a different malloc library than we'd have to re-evaluate all the
  // thresholds in the tests referencing MemoryTest.
  return Mode::Disabled;
#else
  // Even when using TCMALLOC is defined, it appears that
  // Memory::Stats::totalCurrentlyAllocated() does not work as expected
  // on some platforms, so try to force-allocate some heap memory
  // and determine whether we can measure it.
  const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();
  volatile std::unique_ptr<std::string> long_string = std::make_unique<std::string>(
      "more than 22 chars to exceed libc++ short-string optimization");
  const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
  bool can_measure_memory = end_mem > start_mem;

  if (getenv("ENVOY_MEMORY_TEST_EXACT") != nullptr) { // Set in "ci/do_ci.sh" for 'release' tests.
    RELEASE_ASSERT(can_measure_memory,
                   "$ENVOY_MEMORY_TEST_EXACT is set for canonical memory measurements, "
                   "but memory measurement looks broken");
    return Mode::Canonical;
  } else {
    // Different versions of STL and other compiler/architecture differences may
    // also impact memory usage, so when not compiling with MEMORY_TEST_EXACT,
    // memory comparisons must be given some slack. There have recently emerged
    // some memory-allocation differences between development and Envoy CI and
    // Bazel CI (which compiles Envoy as a test of Bazel).
    return can_measure_memory ? Mode::Approximate : Mode::Disabled;
  }
#endif
}

OptionalCounter TestStatStore::findCounter(absl::string_view name) {
  if (store_.numCounters() != num_counters_) {
    ENVOY_LOG_MISC(error, "recomputing counter table");
    for (CounterSharedPtr& counter : store_.counters()) {
      counters_[counter->name()] = counter.get();
    }
    num_counters_ = store_.numCounters();
  }
  auto p = counters_.find(name);
  OptionalCounter ret;
  if (p != counters_.end()) {
    ret = *p->second;
  }
  return ret;
}

Gauge& TestStatStore::gauge(absl::string_view name) {
  if (store_.numGauges() != num_gauges_) {
    ENVOY_LOG_MISC(error, "recomputing gauge table");
    for (GaugeSharedPtr& gauge : store_.gauges()) {
      gauges_[gauge->name()] = gauge.get();
    }
    num_gauges_ = store_.numGauges();
  }
  auto p = gauges_.find(name);
  RELEASE_ASSERT(p != gauges_.end(), absl::StrCat("cannot find gauge: ", name));
  return *p->second;
}

// TODO(jmarantz): this utility is intended to be used both for unit tests
// and fuzz tests. But those have different checking macros, e.g. EXPECT_EQ vs
// FUZZ_ASSERT.
std::vector<uint8_t> serializeDeserializeNumber(uint64_t number) {
  uint64_t num_bytes = SymbolTableImpl::Encoding::encodingSizeBytes(number);
  const uint64_t block_size = 10;
  MemBlockBuilder<uint8_t> mem_block(block_size);
  SymbolTableImpl::Encoding::appendEncoding(number, mem_block);
  num_bytes += mem_block.capacityRemaining();
  RELEASE_ASSERT(block_size == num_bytes, absl::StrCat("Encoding size issue: block_size=",
                                                       block_size, " num_bytes=", num_bytes));
  absl::Span<uint8_t> span = mem_block.span();
  RELEASE_ASSERT(number == SymbolTableImpl::Encoding::decodeNumber(span.data()).first, "");
  return std::vector<uint8_t>(span.data(), span.data() + span.size());
}

void serializeDeserializeString(absl::string_view in) {
  MemBlockBuilder<uint8_t> mem_block(SymbolTableImpl::Encoding::totalSizeBytes(in.size()));
  SymbolTableImpl::Encoding::appendEncoding(in.size(), mem_block);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  mem_block.appendData(absl::MakeSpan(data, data + in.size()));
  RELEASE_ASSERT(mem_block.capacityRemaining() == 0, "");
  absl::Span<uint8_t> span = mem_block.span();
  const std::pair<uint64_t, uint64_t> number_consumed =
      SymbolTableImpl::Encoding::decodeNumber(span.data());
  RELEASE_ASSERT(number_consumed.first == in.size(), absl::StrCat("size matches: ", in));
  span.remove_prefix(number_consumed.second);
  const absl::string_view out(reinterpret_cast<const char*>(span.data()), span.size());
  RELEASE_ASSERT(in == out, absl::StrCat("'", in, "' != '", out, "'"));
}

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
