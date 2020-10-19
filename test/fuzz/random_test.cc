#include "test/fuzz/random.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Fuzz {

// Test the subset selection - since selection is based on a passed in random bytestring you can
// work the algorithm yourself Pass in 5 elements, expect first subset to be element 2 and element
// 4, second subset to be elements 1, 2, 3
TEST(BasicSubsetSelection, RandomTest) {
  // \x01 chooses the first element, which gets swapped with last element, 0x3 chooses the third
  // index, which gets swapped with last element etc.
  std::string random_bytestring = "\x01\x03\x09\x04\x33";
  ProperSubsetSelector subset_selector(random_bytestring);
  const std::vector<std::vector<uint8_t>> subsets = subset_selector.constructSubsets({2, 3}, 5);
  std::vector<uint8_t> subset_one = {1, 3};
  std::vector<uint8_t> subset_two = {0, 2, 4};
  ASSERT_EQ(subsets[0], subset_one);
  ASSERT_EQ(subsets[1], subset_two);
}

} // namespace Fuzz
} // namespace Envoy
