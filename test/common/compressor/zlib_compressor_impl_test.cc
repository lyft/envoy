#include <random>
#include <string>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/compressor/zlib_compressor_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class ZlibCompressorImplTest : public testing::Test {
protected:
  static const int8_t gzip_window_bits{31};
  static const int8_t memory_level{8};
};

class ZlibCompressorImplDeathTest : public ZlibCompressorImplTest {
protected:
  static void compressorBadInitTestHelper(int8_t window_bits, int8_t mem_level) {
    ZlibCompressorImpl compressor;
    compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                    ZlibCompressorImpl::CompressionStrategy::Standard, window_bits, mem_level);
  }

  static void unitializedCompressorTestHelper() {
    Buffer::OwnedImpl input_buffer;
    Buffer::OwnedImpl output_buffer;
    ZlibCompressorImpl compressor;
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 100);
    compressor.compress(input_buffer, output_buffer);
  }
};

TEST_F(ZlibCompressorImplDeathTest, CompressorTestDeath) {
  EXPECT_DEATH(compressorBadInitTestHelper(100, 8), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(compressorBadInitTestHelper(31, 10), std::string{"assert failure: result >= 0"});
  EXPECT_DEATH(unitializedCompressorTestHelper(), std::string{"assert failure: result == Z_OK"});
}

TEST_F(ZlibCompressorImplTest, CompressWithSmallChunkMemory) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor(768);
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  for (uint64_t i = 0; i < 50; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 4796);
    compressor.compress(input_buffer, output_buffer);
    input_buffer.drain(4796);
    ASSERT_EQ(0, input_buffer.length());
  }

  compressor.flush(output_buffer);
  ASSERT_TRUE(output_buffer.length() > 0);

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

TEST_F(ZlibCompressorImplTest, CompressFlushAndCompressMore) {
  Buffer::OwnedImpl input_buffer;
  Buffer::OwnedImpl temp_buffer;
  Buffer::OwnedImpl output_buffer;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  for (uint64_t i = 0; i < 50; i++) {
    TestUtility::feedBufferWithRandomCharacters(input_buffer, 4796);
    compressor.compress(input_buffer, temp_buffer);
    input_buffer.drain(4796);
    ASSERT_EQ(0, input_buffer.length());
    output_buffer.move(temp_buffer);
    ASSERT_EQ(0, temp_buffer.length());
  }

  compressor.flush(temp_buffer);
  ASSERT_TRUE(temp_buffer.length() > 0);

  output_buffer.move(temp_buffer);
  ASSERT_EQ(0, temp_buffer.length());

  TestUtility::feedBufferWithRandomCharacters(input_buffer, 4796);
  compressor.compress(input_buffer, temp_buffer);
  input_buffer.drain(4796);
  output_buffer.move(temp_buffer);

  compressor.flush(temp_buffer);
  ASSERT_TRUE(temp_buffer.length() > 0);

  output_buffer.move(temp_buffer);
  ASSERT_EQ(0, temp_buffer.length());

  uint64_t num_comp_slices = output_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  output_buffer.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

} // namespace
} // namespace Compressor
} // namespace Envoy
