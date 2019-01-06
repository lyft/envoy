#include "common/memory/debug.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Memory {

#ifdef ENVOY_MEMORY_DEBUG_ENABLED

constexpr int ArraySize = 10;

struct MyStruct {
  MyStruct() : x_(0) {} // words_ is uninitialized; will have whatever allocator left there.
  uint64_t x_;
  uint64_t words_[ArraySize];
};

TEST(MemoryDebug, ByteSize) {
  uint64_t before = Debug::bytesUsed();
  auto ptr = std::make_unique<MyStruct>();
  uint64_t after = Debug::bytesUsed();
  EXPECT_EQ(sizeof(MyStruct), after - before);
}

TEST(MemoryDebug, ScribbleOnNew) {
  auto ptr = std::make_unique<MyStruct>();
  for (int i = 0; i < ArraySize; ++i) {
    EXPECT_EQ(0xfeedfacefeedface, ptr->words_[i]);
  }
}

TEST(MemoryDebug, ZeroByteAlloc) { auto ptr = std::make_unique<uint8_t[]>(0); }

#endif // ENVOY_MEMORY_DEBUG_ENABLED

} // namespace Memory
} // namespace Envoy
