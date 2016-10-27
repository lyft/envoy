#pragma once

#include "envoy/common/optional.h"
#include "envoy/network/connection.h"
#include "envoy/ssl/context.h"
#include "envoy/upstream/load_balancer_type.h"
#include "envoy/upstream/resource_manager.h"

namespace Upstream {

/**
 * An upstream host.
 */
class Host : virtual public HostDescription {
public:
  struct CreateConnectionData {
    Network::ClientConnectionPtr connection_;
    HostDescriptionPtr host_description_;
  };

  enum class HealthFlag {
    // The host is currently failing active health checks.
    FAILED_ACTIVE_HC = 0x1,
    // The host is currently considered an outlier and has been ejected.
    FAILED_OUTLIER_CHECK = 0x02
  };

  /**
   * @return host specific counters.
   */
  virtual std::list<std::reference_wrapper<Stats::Counter>> counters() const PURE;

  /**
   * Create a connection for this host.
   * @param dispatcher supplies the owning dispatcher.
   * @return the connection data which includes the raw network connection as well as the *real*
   *         host that backs it. The reason why a 2nd host is returned is that some hosts are
   *         logical and wrap multiple real network destinations. In this case, a different host
   *         will be returned along with the connection vs. the host the method was called on.
   *         If it matters, callers should not assume that the returned host will be the same.
   */
  virtual CreateConnectionData createConnection(Event::Dispatcher& dispatcher) const PURE;

  /**
   * @return host specific gauges.
   */
  virtual std::list<std::reference_wrapper<Stats::Gauge>> gauges() const PURE;

  /**
   * Atomically clear a health flag for a host. Flags are specified in HealthFlags.
   */
  virtual void healthFlagClear(HealthFlag flag) PURE;

  /**
   * Atomically get whether a health flag is set for a host. Flags are specified in HealthFlags.
   */
  virtual bool healthFlagGet(HealthFlag flag) const PURE;

  /**
   * Atomically set a health flag for a host. Flags are specified in HealthFlags.
   */
  virtual void healthFlagSet(HealthFlag flag) PURE;

  /**
   * @return whether in aggregate a host is healthy and routable. Multiple health flags and other
   *         information may be considered.
   */
  virtual bool healthy() const PURE;

  /**
   * Set the host's outlier detector. Outlier detectors are assumed to be thread safe, however
   * a new outlier detector must be installed before the host is used across threads. Thus,
   * this routine should only be called on the main thread before the host is used across threads.
   */
  virtual void setOutlierDetector(OutlierDetectorHostSinkPtr&& outlier_detector) PURE;

  /**
   * @return the current load balancing weight of the host, in the range 1-100.
   */
  virtual uint32_t weight() const PURE;

  /**
   * Set the current load balancing weight of the host, in the range 1-100.
   */
  virtual void weight(uint32_t new_weight) PURE;
};

typedef std::shared_ptr<const Host> ConstHostPtr;

/**
 * Base host set interface. This is used both for clusters, as well as per thread/worker host sets
 * used during routing/forwarding.
 */
class HostSet {
public:
  virtual ~HostSet() {}

  /**
   * Called when cluster host membership is about to change.
   * @param hosts_added supplies the newly added hosts, if any.
   * @param hosts_removed supplies the removed hosts, if any.
   */
  typedef std::function<void(const std::vector<HostPtr>& hosts_added,
                             const std::vector<HostPtr>& hosts_removed)> MemberUpdateCb;

  /**
   * Install a callback that will be invoked when the cluster membership changes.
   * @param callback supplies the callback to invoke.
   */
  virtual void addMemberUpdateCb(MemberUpdateCb callback) const PURE;

  /**
   * @return all hosts that make up the set at the current time.
   */
  virtual const std::vector<HostPtr>& hosts() const PURE;

  /**
   * @return all healthy hosts contained in the set at the current time. NOTE: This set is
   *         eventually consistent. There is a time window where a host in this set may become
   *         unhealthy and calling healthy() on it will return false. Code should be written to
   *         deal with this case if it matters.
   */
  virtual const std::vector<HostPtr>& healthyHosts() const PURE;

  /**
   * @return all hosts that are in the zone local to this node. Required --service-zone to be
   *         set on the command line and to use a cluster type that supports population such as
   *         the SDS cluster type.
   */

  /**
   * @return hosts per zone, index 0 is dedicated to local zone hosts.
   * If there are no hosts in local zone for upstream cluster hostPerZone() will @return
   * empty vector.
   *
   * Note, that we sort zones in alphabetical order starting from index 1.
   */
  virtual const std::vector<std::vector<HostPtr>>& hostsPerZone() const PURE;

  /**
   * @return same as hostsPerZone but only contains healthy hosts.
   */
  virtual const std::vector<std::vector<HostPtr>>& healthyHostsPerZone() const PURE;
};

/**
 * All cluster stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CLUSTER_STATS(COUNTER, GAUGE, TIMER)                                                   \
  COUNTER(upstream_cx_total)                                                                       \
  GAUGE  (upstream_cx_active)                                                                      \
  COUNTER(upstream_cx_http1_total)                                                                 \
  COUNTER(upstream_cx_http2_total)                                                                 \
  COUNTER(upstream_cx_connect_fail)                                                                \
  COUNTER(upstream_cx_connect_timeout)                                                             \
  COUNTER(upstream_cx_overflow)                                                                    \
  TIMER  (upstream_cx_connect_ms)                                                                  \
  TIMER  (upstream_cx_length_ms)                                                                   \
  COUNTER(upstream_cx_destroy)                                                                     \
  COUNTER(upstream_cx_destroy_local)                                                               \
  COUNTER(upstream_cx_destroy_remote)                                                              \
  COUNTER(upstream_cx_destroy_with_active_rq)                                                      \
  COUNTER(upstream_cx_destroy_local_with_active_rq)                                                \
  COUNTER(upstream_cx_destroy_remote_with_active_rq)                                               \
  COUNTER(upstream_cx_close_header)                                                                \
  COUNTER(upstream_cx_rx_bytes_total)                                                              \
  GAUGE  (upstream_cx_rx_bytes_buffered)                                                           \
  COUNTER(upstream_cx_tx_bytes_total)                                                              \
  GAUGE  (upstream_cx_tx_bytes_buffered)                                                           \
  COUNTER(upstream_cx_protocol_error)                                                              \
  COUNTER(upstream_cx_max_requests)                                                                \
  COUNTER(upstream_cx_none_healthy)                                                                \
  COUNTER(upstream_rq_total)                                                                       \
  GAUGE  (upstream_rq_active)                                                                      \
  COUNTER(upstream_rq_pending_total)                                                               \
  COUNTER(upstream_rq_pending_overflow)                                                            \
  COUNTER(upstream_rq_pending_failure_eject)                                                       \
  GAUGE  (upstream_rq_pending_active)                                                              \
  COUNTER(upstream_rq_cancelled)                                                                   \
  COUNTER(upstream_rq_timeout)                                                                     \
  COUNTER(upstream_rq_per_try_timeout)                                                             \
  COUNTER(upstream_rq_rx_reset)                                                                    \
  COUNTER(upstream_rq_tx_reset)                                                                    \
  COUNTER(upstream_rq_retry)                                                                       \
  COUNTER(upstream_rq_retry_success)                                                               \
  COUNTER(upstream_rq_retry_overflow)                                                              \
  COUNTER(upstream_rq_lb_healthy_panic)                                                            \
  COUNTER(membership_change)                                                                       \
  GAUGE  (membership_total)                                                                        \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)                                                                          \
  COUNTER(zone_cluster_too_small)                                                                  \
  COUNTER(zone_routing_all_directly)                                                               \
  COUNTER(zone_routing_sampled)                                                                    \
  COUNTER(zone_routing_cross_zone)                                                                 \
  GAUGE  (max_host_weight)                                                                         \
  COUNTER(local_cluster_not_ok)                                                                    \
  COUNTER(zone_number_differs)
// clang-format on

/**
 * Struct definition for all cluster stats. @see stats_macros.h
 */
struct ClusterStats {
  ALL_CLUSTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

/**
 * An upstream cluster (group of hosts). This class is the "primary" singleton cluster used amongst
 * all forwarding threads/workers. Individual HostSets are used on the workers themselves.
 */
class Cluster : public virtual HostSet {
public:
  struct Features {
    // Whether the upstream supports HTTP2. This is used when creating connection pools.
    static const uint64_t HTTP2 = 0x1;
  };

  /**
   * @return const std::string& the alternate stat name to write cluster stats to. This is useful
   *         during parallel rollouts.
   */
  virtual const std::string& altStatName() const PURE;

  /**
   * @return the connect timeout for upstream hosts that belong to this cluster.
   */
  virtual std::chrono::milliseconds connectTimeout() const PURE;

  /**
   * @return uint64_t features supported by the cluster. @see Features.
   */
  virtual uint64_t features() const PURE;

  /**
   * @return uint64_t HTTP codec options for HTTP connections created on behalf of this cluster.
   *         @see Http::CodecOptions.
   */
  virtual uint64_t httpCodecOptions() const PURE;

  /**
   * Set a callback that will be invoked after the cluster has undergone first time initialization.
   * E.g., for a dynamic DNS cluster the initialize callback will be called when initial DNS
   * resolution is complete.
   */
  virtual void setInitializedCb(std::function<void()> callback) PURE;

  /**
   * @return the SSL context to use when communicating with the cluster.
   */
  virtual Ssl::ClientContext* sslContext() const PURE;

  /**
   * @return the type of load balancing that the cluster should use.
   */
  virtual LoadBalancerType lbType() const PURE;

  /**
   * @return uint64_t the maximum number of outbound requests that a connection pool will make on
   *         each upstream connection. This can be used to increase spread if the backends cannot
   *         tolerate imbalance. 0 indicates no maximum.
   */
  virtual uint64_t maxRequestsPerConnection() const PURE;

  /**
   * @return the human readable name of the cluster.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return ResourceManager& the resource manager to use by proxy agents for for this cluster (at
   *         a particular priority).
   */
  virtual ResourceManager& resourceManager(ResourcePriority priority) const PURE;

  /**
   * Shutdown the cluster prior to destroying connection pools and other thread local data.
   */
  virtual void shutdown() PURE;

  /**
   * @return ClusterStats& strongly named stats for this cluster.
   */
  virtual ClusterStats& stats() const PURE;
};

typedef std::shared_ptr<Cluster> ClusterPtr;
typedef std::shared_ptr<const Cluster> ConstClusterPtr;

} // Upstream
