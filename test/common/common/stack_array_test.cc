#include "common/common/stack_array.h"

#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

namespace Envoy {

class TestEntry {
public:
  TestEntry() { self_ = this; }
  ~TestEntry() { destructor_(val_); }

  int val_ = 0;
  TestEntry* self_;
  MOCK_METHOD1(destructor_, void(int));
};

TEST_F(TestBase, StackArray_ConstructorsAndDestructorsCalled) {
  STACK_ARRAY(entries, TestEntry, 10);

  for (TestEntry& entry : entries) {
    ASSERT_EQ(&entry, entry.self_);
    EXPECT_CALL(entry, destructor_(0)).Times(1);
  }
}

TEST_F(TestBase, StackArray_Modification) {
  STACK_ARRAY(entries, TestEntry, 10);

  int i = 0;
  for (TestEntry& entry : entries) {
    entry.val_ = i;
    EXPECT_CALL(entries[i], destructor_(i)).Times(1);
    i++;
  }
}

} // namespace Envoy
