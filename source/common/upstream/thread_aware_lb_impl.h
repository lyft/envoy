#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/upstream/load_balancer_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

using NormalizedHostWeightVector = std::vector<std::pair<HostConstSharedPtr, double>>;
using NormalizedHostWeightVectorPtr = std::shared_ptr<NormalizedHostWeightVector>;
using NormalizedHostWeightMap = std::map<HostConstSharedPtr, double>;

class ThreadAwareLoadBalancerBase : public LoadBalancerBase, public ThreadAwareLoadBalancer {
public:
  /**
   * Base class for a hashing load balancer implemented for use in a thread aware load balancer.
   * TODO(mattklein123): Currently only RingHash and Maglev use the thread aware load balancer.
   *                     The hash is pre-computed prior to getting to the real load balancer for
   *                     use in priority selection. In the future we likely we will want to pass
   *                     through the full load balancer context in case a future implementation
   *                     wants to use it.
   */
  class HashingLoadBalancer {
  public:
    virtual ~HashingLoadBalancer() = default;
    virtual HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const PURE;
  };
  using HashingLoadBalancerSharedPtr = std::shared_ptr<HashingLoadBalancer>;

  using HostOverloadedPredicate = std::function<bool(HostConstSharedPtr host, double weight)>;
  class BoundedLoadHashingLoadBalancer : public HashingLoadBalancer {
  public:
    /**
     * Class for consistent hashing load balancer (CH-LB) with bounded loads.
     * It is common to both RingHash and Maglev load balancers, because the logic of selecting the
     * next host when one is overloaded is independent of the CH-LB type.
     */
    BoundedLoadHashingLoadBalancer(HashingLoadBalancerSharedPtr hlb_ptr,
                                   const NormalizedHostWeightVectorPtr normalized_host_weight,
                                   uint32_t hash_balanee_factor)
        : BoundedLoadHashingLoadBalancer(hlb_ptr, normalized_host_weight, hash_balanee_factor,
                                         nullptr) {}
    BoundedLoadHashingLoadBalancer(HashingLoadBalancerSharedPtr hlb_ptr,
                                   const NormalizedHostWeightVectorPtr normalized_host_weight,
                                   uint32_t hash_balance_factor,
                                   HostOverloadedPredicate is_host_overloaded)
        : hlb_ptr(hlb_ptr), normalized_host_weights_(normalized_host_weight),
          hash_balance_factor(hash_balance_factor), is_host_overloaded_(is_host_overloaded) {
      ASSERT(hash_balance_factor > 0);
      ASSERT(normalized_host_weights_ != nullptr);
      for (auto const& item : *normalized_host_weights_) {
        normalized_host_weights_map_[item.first] = item.second;
      }
    }
    virtual ~BoundedLoadHashingLoadBalancer() = default;
    virtual HostConstSharedPtr chooseHost(uint64_t hash, uint32_t attempt) const;

  private:
    bool isHostOverloaded(HostConstSharedPtr host, double weight) const;
    HashingLoadBalancerSharedPtr hlb_ptr;
    NormalizedHostWeightVectorPtr normalized_host_weights_;
    NormalizedHostWeightMap normalized_host_weights_map_;
    uint32_t hash_balance_factor;
    HostOverloadedPredicate is_host_overloaded_;
  };
  // Upstream::ThreadAwareLoadBalancer
  LoadBalancerFactorySharedPtr factory() override { return factory_; }
  void initialize() override;

  // Upstream::LoadBalancerBase
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext*) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

protected:
  ThreadAwareLoadBalancerBase(
      const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
      Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : LoadBalancerBase(priority_set, stats, runtime, random, common_config),
        factory_(new LoadBalancerFactoryImpl(stats, random)) {}

private:
  struct PerPriorityState {
    std::shared_ptr<HashingLoadBalancer> current_lb_;
    bool global_panic_{};
  };
  using PerPriorityStatePtr = std::unique_ptr<PerPriorityState>;

  struct LoadBalancerImpl : public LoadBalancer {
    LoadBalancerImpl(ClusterStats& stats, Random::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancer
    HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

    ClusterStats& stats_;
    Random::RandomGenerator& random_;
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_;
    std::shared_ptr<HealthyLoad> healthy_per_priority_load_;
    std::shared_ptr<DegradedLoad> degraded_per_priority_load_;
  };

  struct LoadBalancerFactoryImpl : public LoadBalancerFactory {
    LoadBalancerFactoryImpl(ClusterStats& stats, Random::RandomGenerator& random)
        : stats_(stats), random_(random) {}

    // Upstream::LoadBalancerFactory
    LoadBalancerPtr create() override;

    ClusterStats& stats_;
    Random::RandomGenerator& random_;
    absl::Mutex mutex_;
    std::shared_ptr<std::vector<PerPriorityStatePtr>> per_priority_state_ ABSL_GUARDED_BY(mutex_);
    // This is split out of PerPriorityState so LoadBalancerBase::ChoosePriority can be reused.
    std::shared_ptr<HealthyLoad> healthy_per_priority_load_ ABSL_GUARDED_BY(mutex_);
    std::shared_ptr<DegradedLoad> degraded_per_priority_load_ ABSL_GUARDED_BY(mutex_);
  };

  virtual HashingLoadBalancerSharedPtr
  createLoadBalancer(const NormalizedHostWeightVectorPtr normalized_host_weights,
                     double min_normalized_weight, double max_normalized_weight) PURE;
  void refresh();

  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
};

} // namespace Upstream
} // namespace Envoy
