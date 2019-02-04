#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

using Envoy::Http::HeaderMap;

namespace Envoy {

TEST_F(TestBase, headerMapEqualIgnoreOrder_ActuallyEqual) {
  Http::TestHeaderMapImpl lhs{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  Http::TestHeaderMapImpl rhs{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(TestUtility::headerMapEqualIgnoreOrder(lhs, rhs));
  EXPECT_EQ(lhs, rhs);
}

TEST_F(TestBase, headerMapEqualIgnoreOrder_IgnoreOrder) {
  Http::TestHeaderMapImpl lhs{{":method", "GET"}, {":authority", "host"}, {":path", "/"}};
  Http::TestHeaderMapImpl rhs{{":method", "GET"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_TRUE(TestUtility::headerMapEqualIgnoreOrder(lhs, rhs));
  EXPECT_THAT(&lhs, HeaderMapEqualIgnoreOrder(&rhs));
  EXPECT_FALSE(lhs == rhs);
}

TEST_F(TestBase, headerMapEqualIgnoreOrder_NotEqual) {
  Http::TestHeaderMapImpl lhs{{":method", "GET"}, {":authority", "host"}, {":authority", "host"}};
  Http::TestHeaderMapImpl rhs{{":method", "GET"}, {":authority", "host"}};
  EXPECT_FALSE(TestUtility::headerMapEqualIgnoreOrder(lhs, rhs));
}
} // namespace Envoy
