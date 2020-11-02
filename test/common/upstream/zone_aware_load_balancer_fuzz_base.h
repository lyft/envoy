#include "test/mocks/upstream/priority_set.h"

#include "load_balancer_fuzz_base.h"

namespace Envoy {
namespace Upstream {
class ZoneAwareLoadBalancerFuzzBase : public LoadBalancerFuzzBase {
public:
  ZoneAwareLoadBalancerFuzzBase(bool need_local_cluster, const std::string& random_bytestring)
      : random_bytestring_(random_bytestring) {
    if (need_local_cluster) {
      local_priority_set_ = std::make_shared<PrioritySetImpl>();
      local_priority_set_->getOrCreateHostSet(0);
    }
  }

  ~ZoneAwareLoadBalancerFuzzBase() override {
    // This deletes the load balancer first. If constructed with a local priority set, load balancer
    // with reference local priority set on destruction. Since local priority set is in a base
    // class, it will be initialized second and thus destructed first. Thus, in order to avoid a use
    // after free, you must delete the load balancer before deleting the priority set.
    lb_.reset();
  }

  // These extend base class logic in order to handle local_priority_set_ if applicable.
  void
  initializeASingleHostSet(const test::common::upstream::SetupPriorityLevel& setup_priority_level,
                           const uint8_t priority_level, uint16_t& port) override;

  void initializeLbComponents(const test::common::upstream::LoadBalancerTestCase& input) override;

  void updateHealthFlagsForAHostSet(
      const uint64_t host_priority, const uint32_t num_healthy_hosts,
      const uint32_t num_degraded_hosts, const uint32_t num_excluded_hosts,
      const Protobuf::RepeatedField<Protobuf::uint32>& random_bytestring) override;

  void setupZoneAwareLoadBalancingSpecificLogic();

  void addWeightsToHosts();

  // If fuzzing Zone Aware Load Balancers, local priority set will get constructed sometimes. If not
  // constructed, a local_priority_set_.get() call will return a nullptr.
  std::shared_ptr<PrioritySetImpl> local_priority_set_;

  void clearStaticHostsState() override;

private:
  // This bytestring will be iterated through representing randomness in order to choose
  // weights for hosts.
  const std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};
} // namespace Upstream
} // namespace Envoy
