#pragma once

#include "common/common/logger.h"
#include "common/memory/stats.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

/**
 * Determines whether the library has deterministic malloc-stats results.
 *
 * @return bool true if the Memory::Stats::totalCurrentlyAllocated() has stable results.
 */
bool hasDeterministicMallocStats();

/**
 * Calls fn for a sampling of plausible stat names given a number of clusters.
 * This is intended for memory and performance benchmarking, where the syntax of
 * the names may be material to the measurements. Here we are deliberately not
 * claiming this is a complete stat set, which will change over time. Instead we
 * are aiming for consistency over time in order to create unit tests against
 * fixed memory budgets.
 *
 * @param num_clusters the number of clusters for which to generate stats.
 * @param fn the function to call with every stat name.
 */
void forEachSampleStat(int num_clusters, std::function<void(absl::string_view)> fn);

// Defines a test-macro for expected memory consumption. There are 3 cases:
//   1. Memory usage API is available, and is built using with a canonical
//      toolchain, enabling exact comparisons against an expected number of
//      bytes consumed. The canonical environment is Envoy CI release builds.
//   2. Memory usage API is available, but the current build may subtly differ
//      in memory consumption from #1. We'd still like to track memory usage
//      but it needs to be approximate.
//   3. Memory usage API is not available. In this case, the code is executed
//      but no testing occurs.
class MemoryTest {
public:
  enum class Mode {
    Disabled,    // No memory usage data available on platform.
    Canonical,   // Memory usage is available, and current platform is canonical.
    Approximate, // Memory usage is available, but variances form canonical expected.
  };

  MemoryTest() : memory_at_construction_(Memory::Stats::totalCurrentlyAllocated()) {}

  static Mode mode();

  size_t consumedBytes() const {
    return Memory::Stats::totalCurrentlyAllocated() - memory_at_construction_;
  }

private:
  const size_t memory_at_construction_;
};

#define EXPECT_MEMORY_EQ(consumed_bytes, expected_value)                                           \
  do {                                                                                             \
    if (Stats::TestUtil::MemoryTest::mode() == Stats::TestUtil::MemoryTest::Mode::Canonical) {     \
      EXPECT_EQ(consumed_bytes, expected_value);                                                   \
    } else {                                                                                       \
      ENVOY_LOG_MISC(info,                                                                         \
                     "Skipping exact memory test against {} bytes as platform is non-canonical",   \
                     expected_value);                                                              \
    }                                                                                              \
  } while (false)

#define EXPECT_MEMORY_LE(consumed_bytes, expected_value)                                           \
  do {                                                                                             \
    if (Stats::TestUtil::MemoryTest::mode() != Stats::TestUtil::MemoryTest::Mode::Disabled) {      \
      EXPECT_LE(consumed_bytes, expected_value);                                                   \
    } else {                                                                                       \
      ENVOY_LOG_MISC(                                                                              \
          info, "Skipping approximate memory test against {} bytes as platform lacks tcmalloc",    \
          expected_value);                                                                         \
    }                                                                                              \
  } while (false)

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
