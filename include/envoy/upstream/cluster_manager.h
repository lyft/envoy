#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/conn_pool.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/admin.h"
#include "envoy/server/options.h"
#include "envoy/singleton/manager.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/store.h"
#include "envoy/stats/symbol_table.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Upstream {

/**
 * ClusterUpdateCallbacks provide a way to expose Cluster lifecycle events in the
 * ClusterManager.
 */
class ClusterUpdateCallbacks {
public:
  virtual ~ClusterUpdateCallbacks() = default;

  /**
   * onClusterAddOrUpdate is called when a new cluster is added or an existing cluster
   * is updated in the ClusterManager.
   * @param cluster is the ThreadLocalCluster that represents the updated
   * cluster.
   */
  virtual void onClusterAddOrUpdate(ThreadLocalCluster& cluster) PURE;

  /**
   * onClusterRemoval is called when a cluster is removed; the argument is the cluster name.
   * @param cluster_name is the name of the removed cluster.
   */
  virtual void onClusterRemoval(const std::string& cluster_name) PURE;
};

/**
 * ClusterUpdateCallbacksHandle is a RAII wrapper for a ClusterUpdateCallbacks. Deleting
 * the ClusterUpdateCallbacksHandle will remove the callbacks from ClusterManager in O(1).
 */
class ClusterUpdateCallbacksHandle {
public:
  virtual ~ClusterUpdateCallbacksHandle() = default;
};

using ClusterUpdateCallbacksHandlePtr = std::unique_ptr<ClusterUpdateCallbacksHandle>;

/**
 * Status enum for the result of an attempted cluster discovery.
 */
enum class ClusterDiscoveryStatus {
  /**
   * Cluster was not found during the discovery process.
   */
  Missing,
  /**
   * Cluster found and currently available through ClusterManager.
   */
  Available,
};

/**
 * ClusterDiscoveryCallback is a callback called at the end of the on-demand cluster discovery
 * process. The status of the discovery is sent as a parameter.
 */
using ClusterDiscoveryCallback = std::function<void(ClusterDiscoveryStatus)>;
using ClusterDiscoveryCallbackWeakPtr = std::weak_ptr<ClusterDiscoveryCallback>;
using ClusterDiscoveryCallbackSharedPtr = std::shared_ptr<ClusterDiscoveryCallback>;

/**
 * ClusterDiscoveryCallbackHandle is a RAII wrapper for a ClusterDiscoveryCallback. Deleting the
 * ClusterDiscoveryCallbackHandle will remove the callbacks from ClusterManager.
 */
class ClusterDiscoveryCallbackHandle {
public:
  virtual ~ClusterDiscoveryCallbackHandle() = default;
};

using ClusterDiscoveryCallbackHandlePtr = std::unique_ptr<ClusterDiscoveryCallbackHandle>;

class ClusterManagerFactory;

// These are per-cluster per-thread, so not "global" stats.
struct ClusterConnectivityState {
  ~ClusterConnectivityState() {
    ASSERT(pending_streams_ == 0);
    ASSERT(active_streams_ == 0);
    ASSERT(connecting_stream_capacity_ == 0);
  }

  template <class T> void checkAndDecrement(T& value, uint32_t delta) {
    ASSERT(std::numeric_limits<T>::min() + delta <= value);
    value -= delta;
  }

  template <class T> void checkAndIncrement(T& value, uint32_t delta) {
    ASSERT(std::numeric_limits<T>::max() - delta >= value);
    value += delta;
  }

  void incrPendingStreams(uint32_t delta) { checkAndIncrement(pending_streams_, delta); }
  void decrPendingStreams(uint32_t delta) { checkAndDecrement(pending_streams_, delta); }
  void incrConnectingStreamCapacity(uint32_t delta) {
    checkAndIncrement(connecting_stream_capacity_, delta);
  }
  void decrConnectingStreamCapacity(uint32_t delta) {
    checkAndDecrement(connecting_stream_capacity_, delta);
  }
  void incrActiveStreams(uint32_t delta) { checkAndIncrement(active_streams_, delta); }
  void decrActiveStreams(uint32_t delta) { checkAndDecrement(active_streams_, delta); }

  // Tracks the number of pending streams for this ClusterManager.
  uint32_t pending_streams_{};
  // Tracks the number of active streams for this ClusterManager.
  uint32_t active_streams_{};
  // Tracks the available stream capacity if all connecting connections were connected.
  //
  // For example, if an H2 connection is started with concurrent stream limit of 100, this
  // goes up by 100. If the connection is established and 2 streams are in use, it
  // would be reduced to 98 (as 2 of the 100 are not available).
  //
  // Note that if more HTTP/2 streams have been established than are allowed by
  // a late-received SETTINGS frame, this MAY BE NEGATIVE.
  // Note this tracks the sum of multiple 32 bit stream capacities so must remain 64 bit.
  int64_t connecting_stream_capacity_{};
};

class OdCdsApi;
using OdCdsApiSharedPtr = std::shared_ptr<OdCdsApi>;

/**
 * Manages connection pools and load balancing for upstream clusters. The cluster manager is
 * persistent and shared among multiple ongoing requests/connections.
 * Cluster manager is initialized in two phases. In the first phase which begins at the construction
 * all primary clusters (i.e. with endpoint assignments provisioned statically in bootstrap,
 * discovered through DNS or file based CDS) are initialized. This phase may complete synchronously
 * with cluster manager construction iff all clusters are STATIC and without health checks
 * configured. At the completion of the first phase cluster manager invokes callback set through the
 * `setPrimaryClustersInitializedCb` method.
 * After the first phase has completed the server instance initializes services (i.e. RTDS) needed
 * to successfully deploy the rest of dynamic configuration.
 * In the second phase all secondary clusters (with endpoint assignments provisioned by xDS servers)
 * are initialized and then the rest of the configuration provisioned through xDS.
 */
class ClusterManager {
public:
  using PrimaryClustersReadyCallback = std::function<void()>;
  using InitializationCompleteCallback = std::function<void()>;

  virtual ~ClusterManager() = default;

  /**
   * Add or update a cluster via API. The semantics of this API are:
   * 1) The hash of the config is used to determine if an already existing cluster has changed.
   *    Nothing is done if the hash matches the previously running configuration.
   * 2) Statically defined clusters (those present when Envoy starts) can not be updated via API.
   *
   * @param cluster supplies the cluster configuration.
   * @param version_info supplies the xDS version of the cluster.
   * @return true if the action results in an add/update of a cluster.
   */
  virtual bool addOrUpdateCluster(const envoy::config::cluster::v3::Cluster& cluster,
                                  const std::string& version_info) PURE;

  /**
   * Set a callback that will be invoked when all primary clusters have been initialized.
   */
  virtual void setPrimaryClustersInitializedCb(PrimaryClustersReadyCallback callback) PURE;

  /**
   * Set a callback that will be invoked when all owned clusters have been initialized.
   */
  virtual void setInitializedCb(InitializationCompleteCallback callback) PURE;

  /**
   * Start initialization of secondary clusters and then dynamically configured clusters.
   * The "initialized callback" set in the method above is invoked when secondary and
   * dynamically provisioned clusters have finished initializing.
   */
  virtual void
  initializeSecondaryClusters(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) PURE;

  using ClusterInfoMap = absl::flat_hash_map<std::string, std::reference_wrapper<const Cluster>>;
  struct ClusterInfoMaps {
    bool hasCluster(absl::string_view cluster) const {
      return active_clusters_.find(cluster) != active_clusters_.end() ||
             warming_clusters_.find(cluster) != warming_clusters_.end();
    }

    ClusterConstOptRef getCluster(absl::string_view cluster) {
      auto active_cluster = active_clusters_.find(cluster);
      if (active_cluster != active_clusters_.end()) {
        return active_cluster->second;
      }
      auto warming_cluster = warming_clusters_.find(cluster);
      if (warming_cluster != warming_clusters_.end()) {
        return warming_cluster->second;
      }
      return absl::nullopt;
    }

    ClusterInfoMap active_clusters_;
    ClusterInfoMap warming_clusters_;
  };

  /**
   * @return ClusterInfoMap all current clusters including active and warming.
   *
   * NOTE: This method is only thread safe on the main thread. It should not be called elsewhere.
   */
  virtual ClusterInfoMaps clusters() PURE;

  using ClusterSet = absl::flat_hash_set<std::string>;

  /**
   * @return const ClusterSet& providing the cluster names that are eligible as
   *         xDS API config sources. These must be static (i.e. in the
   *         bootstrap) and non-EDS.
   */
  virtual const ClusterSet& primaryClusters() PURE;

  /**
   * @return ThreadLocalCluster* the thread local cluster with the given name or nullptr if it
   * does not exist. This is thread safe.
   *
   * NOTE: The pointer returned by this function is ONLY safe to use in the context of the owning
   * call (or if the caller knows that the cluster is fully static and will never be deleted). In
   * the case of dynamic clusters, subsequent event loop iterations may invalidate this pointer.
   * If information about the cluster needs to be kept, use the ThreadLocalCluster::info() method to
   * obtain cluster information that is safe to store.
   *
   * NOTE: This method may return nullptr even if the cluster exists (if it hasn't been warmed yet,
   * propagated to workers, etc.). Use clusters() for general configuration checking on the main
   * thread.
   */
  virtual ThreadLocalCluster* getThreadLocalCluster(absl::string_view cluster) PURE;

  /**
   * Remove a cluster via API. Only clusters added via addOrUpdateCluster() can
   * be removed in this manner. Statically defined clusters present when Envoy starts cannot be
   * removed.
   *
   * @return true if the action results in the removal of a cluster.
   */
  virtual bool removeCluster(const std::string& cluster) PURE;

  /**
   * Shutdown the cluster manager prior to destroying connection pools and other thread local data.
   */
  virtual void shutdown() PURE;

  /**
   * @return const envoy::config::core::v3::BindConfig& cluster manager wide bind configuration for
   * new upstream connections.
   */
  virtual const envoy::config::core::v3::BindConfig& bindConfig() const PURE;

  /**
   * Returns a shared_ptr to the singleton xDS-over-gRPC provider for upstream control plane muxing
   * of xDS. This is treated somewhat as a special case in ClusterManager, since it does not relate
   * logically to the management of clusters but instead is required early in ClusterManager/server
   * initialization and in various sites that need ClusterManager for xDS API interfacing.
   *
   * @return GrpcMux& ADS API provider referencee.
   */
  virtual Config::GrpcMuxSharedPtr adsMux() PURE;

  /**
   * @return Grpc::AsyncClientManager& the cluster manager's gRPC client manager.
   */
  virtual Grpc::AsyncClientManager& grpcAsyncClientManager() PURE;

  /**
   * Return the local cluster name, if it was configured.
   *
   * @return absl::optional<std::string> the local cluster name, or empty if no local cluster was
   * configured.
   */
  virtual const absl::optional<std::string>& localClusterName() const PURE;

  /**
   * This method allows to register callbacks for cluster lifecycle events in the ClusterManager.
   * The callbacks will be registered in a thread local slot and the callbacks will be executed
   * on the thread that registered them.
   * To be executed on all threads, Callbacks need to be registered on all threads.
   *
   * @param callbacks are the ClusterUpdateCallbacks to add or remove to the cluster manager.
   * @return ClusterUpdateCallbacksHandlePtr a RAII that needs to be deleted to
   * unregister the callback.
   */
  virtual ClusterUpdateCallbacksHandlePtr
  addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& callbacks) PURE;

  /**
   * Return the factory to use for creating cluster manager related objects.
   */
  virtual ClusterManagerFactory& clusterManagerFactory() PURE;

  /**
   * Obtain the subscription factory for the cluster manager. Since subscriptions may have an
   * upstream component, the factory is a facet of the cluster manager.
   *
   * @return Config::SubscriptionFactory& the subscription factory.
   */
  virtual Config::SubscriptionFactory& subscriptionFactory() PURE;

  /**
   * Returns a struct with all the Stats::StatName objects needed by
   * Clusters. This helps factor out some relatively heavy name
   * construction which occur when there is a large CDS update during operation,
   * relative to recreating all stats from strings on-the-fly.
   *
   * @return the stat names.
   */
  virtual const ClusterStatNames& clusterStatNames() const PURE;
  virtual const ClusterLoadReportStatNames& clusterLoadReportStatNames() const PURE;
  virtual const ClusterCircuitBreakersStatNames& clusterCircuitBreakersStatNames() const PURE;
  virtual const ClusterRequestResponseSizeStatNames&
  clusterRequestResponseSizeStatNames() const PURE;
  virtual const ClusterTimeoutBudgetStatNames& clusterTimeoutBudgetStatNames() const PURE;

  /**
   * Request an on-demand discovery of a cluster with a passed name. Passed ODCDS may be used to
   * perform the discovery process in the main thread if there is no discovery going on for this
   * cluster. The passed callback will be invoked when the cluster is added and warmed up. It is
   * expected that the callback will be destroyed when it is invoked. To cancel the discovery,
   * destroy the returned handle and the callback.
   *
   * This function is thread-safe.
   *
   * @param odcds is a pointer to ODCDS used for discovery. Must not be a nullptr.
   * @param name is the name of the cluster to be discovered.
   * @param callback will be called when the discovery is finished.
   * @return ClusterDiscoveryCallbackHandlePtr the discovery process handle.
   */
  virtual ClusterDiscoveryCallbackHandlePtr
  requestOnDemandClusterDiscovery(OdCdsApiSharedPtr odcds, const std::string& name,
                                  ClusterDiscoveryCallbackWeakPtr callback) PURE;

  /**
   * Notifies the threads that were waiting for discovery of a cluster with a passed name. Usually
   * used to unblock the requests in case of discovery failure.
   *
   * @param name is the cluster's name.
   */
  virtual void notifyOnDemandCluster(const std::string& name,
                                     ClusterDiscoveryStatus cluster_status) PURE;
};

using ClusterManagerPtr = std::unique_ptr<ClusterManager>;

/**
 * Abstract interface for a CDS API provider.
 */
class CdsApi {
public:
  virtual ~CdsApi() = default;

  /**
   * Start the first fetch of CDS data.
   */
  virtual void initialize() PURE;

  /**
   * Set a callback that will be called when the CDS API has done an initial load from the remote
   * server. If the initial load fails, the callback will also be called.
   */
  virtual void setInitializedCb(std::function<void()> callback) PURE;

  /**
   * @return std::string last accepted version from fetch.
   */
  virtual const std::string versionInfo() const PURE;
};

using CdsApiPtr = std::unique_ptr<CdsApi>;

/**
 * Abstract interface for a On-Demand CDS API provider.
 */
class OdCdsApi {
public:
  virtual ~OdCdsApi() = default;

  /**
   * File an on-demand request for a cluster.
   */
  virtual void updateOnDemand(const std::string& cluster_name) PURE;
};

using OdCdsApiPtr = std::unique_ptr<OdCdsApi>;

/**
 * Factory for objects needed during cluster manager operation.
 */
class ClusterManagerFactory {
public:
  virtual ~ClusterManagerFactory() = default;

  /**
   * Allocate a cluster manager from configuration proto.
   */
  virtual ClusterManagerPtr
  clusterManagerFromProto(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) PURE;

  /**
   * Allocate an HTTP connection pool for the host. Pools are separated by 'priority',
   * 'protocol', and 'options->hashKey()', if any.
   */
  virtual Http::ConnectionPool::InstancePtr
  allocateConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                   ResourcePriority priority, std::vector<Http::Protocol>& protocol,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
                   TimeSource& time_source, ClusterConnectivityState& state) PURE;

  /**
   * Allocate a TCP connection pool for the host. Pools are separated by 'priority' and
   * 'options->hashKey()', if any.
   */
  virtual Tcp::ConnectionPool::InstancePtr
  allocateTcpConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                      ResourcePriority priority,
                      const Network::ConnectionSocket::OptionsSharedPtr& options,
                      Network::TransportSocketOptionsSharedPtr transport_socket_options,
                      ClusterConnectivityState& state) PURE;

  /**
   * Allocate a cluster from configuration proto.
   */
  virtual std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
                   Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) PURE;

  /**
   * Create a CDS API provider from configuration proto.
   */
  virtual CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                              const xds::core::v3::ResourceLocator* cds_resources_locator,
                              ClusterManager& cm) PURE;

  /**
   * Returns the secret manager.
   */
  virtual Secret::SecretManager& secretManager() PURE;
};

/**
 * Factory for creating ClusterInfo
 */
class ClusterInfoFactory {
public:
  virtual ~ClusterInfoFactory() = default;

  /**
   * Parameters for createClusterInfo().
   */
  struct CreateClusterInfoParams {
    Server::Admin& admin_;
    Runtime::Loader& runtime_;
    const envoy::config::cluster::v3::Cluster& cluster_;
    const envoy::config::core::v3::BindConfig& bind_config_;
    Stats::Store& stats_;
    Ssl::ContextManager& ssl_context_manager_;
    const bool added_via_api_;
    ClusterManager& cm_;
    const LocalInfo::LocalInfo& local_info_;
    Event::Dispatcher& dispatcher_;
    Singleton::Manager& singleton_manager_;
    ThreadLocal::SlotAllocator& tls_;
    ProtobufMessage::ValidationVisitor& validation_visitor_;
    Api::Api& api_;
    const Server::Options& options_;
  };

  /**
   * This method returns a Upstream::ClusterInfoConstSharedPtr given construction parameters.
   */
  virtual Upstream::ClusterInfoConstSharedPtr
  createClusterInfo(const CreateClusterInfoParams& params) PURE;
};

} // namespace Upstream
} // namespace Envoy
