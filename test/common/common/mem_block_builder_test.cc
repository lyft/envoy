#include "common/common/mem_block_builder.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(MemBlockBuilderTest, AppendUint8) {
  MemBlockBuilder<uint8_t> mem_block(10);
  EXPECT_EQ(10, mem_block.capacity());
  mem_block.appendOne(5);
  EXPECT_EQ(9, mem_block.capacityRemaining());
  const uint8_t foo[] = {6, 7};
  mem_block.appendData(foo, ABSL_ARRAYSIZE(foo));
  EXPECT_EQ(7, mem_block.capacityRemaining());

  MemBlockBuilder<uint8_t> append;
  EXPECT_EQ(0, append.capacity());
  append.populate(7);
  EXPECT_EQ(7, append.capacity());
  append.appendOne(8);
  append.appendOne(9);
  mem_block.appendBlock(append);

  EXPECT_EQ(5, mem_block.capacityRemaining());
  EXPECT_EQ((std::vector<uint8_t>{5, 6, 7, 8, 9}), mem_block.toVector());

  append.appendBlock(mem_block);
  EXPECT_EQ(0, append.capacityRemaining());
  EXPECT_EQ((std::vector<uint8_t>{8, 9, 5, 6, 7, 8, 9}), append.toVector());

  mem_block.reset();
  EXPECT_EQ(0, mem_block.capacity());
}

TEST(MemBlockBuilderTest, AppendUint32) {
  MemBlockBuilder<uint32_t> mem_block(10);
  EXPECT_EQ(10, mem_block.capacity());
  mem_block.appendOne(100005);
  EXPECT_EQ(9, mem_block.capacityRemaining());
  const uint32_t foo[] = {100006, 100007};
  mem_block.appendData(foo, ABSL_ARRAYSIZE(foo));
  EXPECT_EQ(7, mem_block.capacityRemaining());

  MemBlockBuilder<uint32_t> append;
  EXPECT_EQ(0, append.capacity());
  append.populate(7);
  EXPECT_EQ(7, append.capacity());
  append.appendOne(100008);
  append.appendOne(100009);
  mem_block.appendBlock(append);

  EXPECT_EQ(5, mem_block.capacityRemaining());
  EXPECT_EQ((std::vector<uint32_t>{100005, 100006, 100007, 100008, 100009}), mem_block.toVector());

  append.appendBlock(mem_block);
  EXPECT_EQ(0, append.capacityRemaining());
  EXPECT_EQ((std::vector<uint32_t>{100008, 100009, 100005, 100006, 100007, 100008, 100009}),
            append.toVector());

  mem_block.reset();
  EXPECT_EQ(0, mem_block.capacity());
}

TEST(MemBlockBuilderTest, AppendTooMuch) {
  MemBlockBuilder<uint8_t> mem_block(1);
  mem_block.appendOne(1);
  EXPECT_DEATH({ mem_block.appendOne(2); }, "insufficient capacity");
  const uint8_t foo[] = {3, 4};
  EXPECT_DEATH({ mem_block.appendData(foo, ABSL_ARRAYSIZE(foo)); }, "insufficient capacity");
}

} // namespace Envoy
