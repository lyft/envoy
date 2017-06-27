#pragma once

#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"

#include "common/upstream/upstream_impl.h"

#include "api/eds.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the Endpoint Discovery Service.
 */
class EdsClusterImpl : public BaseDynamicClusterImpl,
                       Config::SubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> {
public:
  EdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                 Ssl::ContextManager& ssl_context_manager, const SdsConfig& sds_config,
                 const LocalInfo::LocalInfo& local_info, ClusterManager& cm,
                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random);

  // Upstream::Cluster
  void initialize() override;
  InitializePhase initializePhase() const override { return InitializePhase::Secondary; }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;

private:
  void runInitializeCallbackIfAny();

  std::unique_ptr<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>> subscription_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  uint64_t pending_health_checks_{};
};

} // Upstream
} // Envoy
