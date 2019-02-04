#include "test/test_common/test_base.h"

namespace Envoy {

TEST_F(TestBase, GccOnly_CompilerCheck) {
#if defined(__clang__) or not defined(__GNUC__)
  // clang is incompatible with gcov.
  FAIL() << "GCC is required for coverage runs";
#endif
}

} // namespace Envoy
