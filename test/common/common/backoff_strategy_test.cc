#include "common/common/backoff_strategy.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/test_base.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {

TEST_F(TestBase, BackOffStrategyTest_JitteredBackOffBasicFlow) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());
}

TEST_F(TestBase, BackOffStrategyTest_JitteredBackOffBasicReset) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(27));

  JitteredBackOffStrategy jittered_back_off(25, 30, random);
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(27, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(2, jittered_back_off.nextBackOffMs()); // Should start from start
}

TEST_F(TestBase, BackOffStrategyTest_JitteredBackOffWithMaxInterval) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(1024));

  JitteredBackOffStrategy jittered_back_off(5, 100, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(49, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(94, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(94, jittered_back_off.nextBackOffMs()); // Should return Max here
}

TEST_F(TestBase, BackOffStrategyTest_JitteredBackOffWithMaxIntervalReset) {
  NiceMock<Runtime::MockRandomGenerator> random;
  ON_CALL(random, random()).WillByDefault(Return(1024));

  JitteredBackOffStrategy jittered_back_off(5, 100, random);
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(9, jittered_back_off.nextBackOffMs());
  EXPECT_EQ(49, jittered_back_off.nextBackOffMs());

  jittered_back_off.reset();
  EXPECT_EQ(4, jittered_back_off.nextBackOffMs()); // Should start from start
}
} // namespace Envoy
