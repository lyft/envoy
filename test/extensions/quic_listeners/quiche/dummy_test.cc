#include "extensions/quic_listeners/quiche/dummy.h"

#include "test/test_common/test_base.h"

#include "quiche/http2/platform/api/http2_string.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

TEST_F(TestBase, DummyTest_Dummy) {
  http2::Http2String foo = "bar";
  EXPECT_EQ("bar cowbell", moreCowbell(foo));
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
