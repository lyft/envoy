#include "server/backtrace.h"

#include "test/test_common/test_base.h"

namespace Envoy {
TEST_F(TestBase, Backward_Basic) {
  // There isn't much to test here and this feature is really just useful for
  // debugging. This test simply verifies that we do not cause a crash when
  // logging a backtrace, and covers the added lines.
  BackwardsTrace tracer;
  tracer.capture();
  tracer.logTrace();
}

TEST_F(TestBase, Backward_InvalidUsageTest) {
  // Ensure we do not crash if logging is attempted when there was no trace captured
  BackwardsTrace tracer;
  tracer.logTrace();
}
} // namespace Envoy
