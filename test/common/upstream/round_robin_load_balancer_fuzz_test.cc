#include <memory>

#include "test/common/upstream/round_robin_load_balancer_fuzz.pb.validate.h"
#include "test/common/upstream/zone_aware_load_balancer_fuzz_base.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::RoundRobinLoadBalancerTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  ZoneAwareLoadBalancerFuzzBase zone_aware_load_balancer_fuzz = ZoneAwareLoadBalancerFuzzBase(
      input.zone_aware_load_balancer_test_case().need_local_priority_set(),
      input.zone_aware_load_balancer_test_case().random_bytestring_for_weights());
  zone_aware_load_balancer_fuzz.initializeLbComponents(
      input.zone_aware_load_balancer_test_case().load_balancer_test_case());
  zone_aware_load_balancer_fuzz.setupZoneAwareLoadBalancingSpecificLogic();

  try {
    zone_aware_load_balancer_fuzz.lb_ = std::make_unique<RoundRobinLoadBalancer>(
        zone_aware_load_balancer_fuzz.priority_set_,
        zone_aware_load_balancer_fuzz.local_priority_set_.get(),
        zone_aware_load_balancer_fuzz.stats_, zone_aware_load_balancer_fuzz.runtime_,
        zone_aware_load_balancer_fuzz.random_,
        input.zone_aware_load_balancer_test_case().load_balancer_test_case().common_lb_config());
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException; {}", e.what());
    return;
  }

  zone_aware_load_balancer_fuzz.replay(
      input.zone_aware_load_balancer_test_case().load_balancer_test_case().actions());
}

} // namespace Upstream
} // namespace Envoy
