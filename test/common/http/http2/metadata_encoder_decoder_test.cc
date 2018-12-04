#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/http/http2/metadata_decoder.h"
#include "common/http/http2/metadata_encoder.h"
#include "common/runtime/runtime_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nghttp2/nghttp2.h"

// A global variable in nghttp2 to disable preface and initial settings for tests.
// TODO(soya3129): Remove after issue https://github.com/nghttp2/nghttp2/issues/1246 is fixed.
extern int nghttp2_enable_strict_preface;

namespace Envoy {
namespace Http {
namespace Http2 {

namespace {
static const uint64_t STREAM_ID = 1;

// The buffer stores data sent by encoder and received by decoder.
typedef struct {
  uint8_t buf[1024 * 1024] = {0};
  size_t length = 0;
} TestBuffer;

// The application data structure passes to nghttp2 session.
typedef struct {
  MetadataEncoder* encoder;
  MetadataDecoder* decoder;
  // Stores data sent by encoder and received by the decoder.
  TestBuffer* output_buffer;
} UserData;

// Nghttp2 callback function for sending extension frame.
static ssize_t pack_extension_callback(nghttp2_session* session, uint8_t* buf, size_t len,
                                       const nghttp2_frame*, void* user_data) {
  EXPECT_NE(nullptr, session);

  MetadataEncoder* encoder = reinterpret_cast<UserData*>(user_data)->encoder;
  const uint64_t size_copied = encoder->packNextFramePayload(buf, len);

  return static_cast<ssize_t>(size_copied);
}

// Nghttp2 callback function for receiving extension frame.
static int on_extension_chunk_recv_callback(nghttp2_session* session, const nghttp2_frame_hd* hd,
                                            const uint8_t* data, size_t len, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_GE(hd->length, len);

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  bool success = decoder->receiveMetadata(data, len);
  return success ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

// Nghttp2 callback function for unpack extension frames.
static int unpack_extension_callback(nghttp2_session* session, void** payload,
                                     const nghttp2_frame_hd* hd, void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_NE(nullptr, hd);
  EXPECT_NE(nullptr, payload);

  MetadataDecoder* decoder = reinterpret_cast<UserData*>(user_data)->decoder;
  bool result = decoder->onMetadataFrameComplete((hd->flags == END_METADATA_FLAG) ? true : false);
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

// Nghttp2 callback function for sending data to peer.
static ssize_t send_callback(nghttp2_session* session, const uint8_t* buf, size_t len, int flags,
                             void* user_data) {
  EXPECT_NE(nullptr, session);
  EXPECT_LE(0, flags);

  TestBuffer* buffer = (reinterpret_cast<UserData*>(user_data))->output_buffer;
  memcpy(buffer->buf + buffer->length, buf, len);
  buffer->length += len;
  return len;
}
} // namespace

class MetadataEncoderDecoderTest : public ::testing::Test {
public:
  void initialize(MetadataCallback cb) {
    decoder_ = std::make_unique<MetadataDecoder>(cb);

    // Enables extension frame.
    nghttp2_option_new(&option_);
    nghttp2_option_set_user_recv_extension_type(option_, METADATA_FRAME_TYPE);

    // Sets callback functions.
    nghttp2_session_callbacks_new(&callbacks_);
    nghttp2_session_callbacks_set_pack_extension_callback(callbacks_, pack_extension_callback);
    nghttp2_session_callbacks_set_send_callback(callbacks_, send_callback);
    nghttp2_session_callbacks_set_on_extension_chunk_recv_callback(
        callbacks_, on_extension_chunk_recv_callback);
    nghttp2_session_callbacks_set_unpack_extension_callback(callbacks_, unpack_extension_callback);

    // Sets application data to pass to nghttp2 session.
    user_data_.encoder = &encoder_;
    user_data_.decoder = decoder_.get();
    user_data_.output_buffer = &output_buffer_;

    // Creates new nghttp2 session.
    nghttp2_enable_strict_preface = 0;
    nghttp2_session_client_new2(&session_, callbacks_, &user_data_, option_);
    nghttp2_enable_strict_preface = 1;
  }

  void cleanUp() {
    nghttp2_session_del(session_);
    nghttp2_session_callbacks_del(callbacks_);
    nghttp2_option_del(option_);
  }

  void verifyMetadataMapVec(MetadataMapVec& expect, std::unique_ptr<MetadataMap> metadata_map) {
    for (const auto& metadata : *metadata_map) {
      EXPECT_EQ(expect.front()->find(metadata.first)->second, metadata.second);
    }
    expect.erase(expect.begin());
  }

  void submitMetadata(const MetadataMapVec& metadata_map_vec) {
    // Creates metadata payload.
    encoder_.createPayload(metadata_map_vec);
    while (encoder_.hasNextFrame()) {
      int result = nghttp2_submit_extension(session_, METADATA_FRAME_TYPE,
                                            encoder_.nextEndMetadata(), STREAM_ID, nullptr);
      EXPECT_EQ(0, result);
      // Sends METADATA to nghttp2.
      result = nghttp2_session_send(session_);
      EXPECT_EQ(0, result);
    }
  }

  nghttp2_session* session_ = nullptr;
  nghttp2_session_callbacks* callbacks_;
  MetadataEncoder encoder_;
  std::unique_ptr<MetadataDecoder> decoder_;
  nghttp2_option* option_;

  // Stores data received by peer.
  TestBuffer output_buffer_;

  // Application data passed to nghttp2.
  UserData user_data_;

  Runtime::RandomGeneratorImpl random_generator_;
};

TEST_F(MetadataEncoderDecoderTest, TestMetadataSizeLimit) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(1024 * 1024 + 1, 'a')},
  };
  MetadataMapVec metadata_map_vec;
  metadata_map_vec.push_back(&metadata_map);

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                  metadata_map_vec, std::placeholders::_1);
  initialize(cb);

  // metadata_map exceeds size limit.
  EXPECT_FALSE(encoder_.createPayload(metadata_map_vec));

  std::string payload = std::string(1024 * 1024 + 1, 'a');
  EXPECT_FALSE(
      decoder_->receiveMetadata(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, TestDecodeBadData) {
  MetadataMap metadata_map = {
      {"header_key1", "header_value1"},
  };
  MetadataMapVec metadata_map_vec;
  metadata_map_vec.push_back(&metadata_map);

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                  metadata_map_vec, std::placeholders::_1);
  initialize(cb);
  submitMetadata(metadata_map_vec);

  // Messes up with the encoded payload, and passes it to the decoder.
  output_buffer_.buf[10] |= 0xff;
  decoder_->receiveMetadata(output_buffer_.buf, output_buffer_.length);
  EXPECT_FALSE(decoder_->onMetadataFrameComplete(true));

  cleanUp();
}

// Checks if accumulated metadata size reaches size limit, returns failure.
TEST_F(MetadataEncoderDecoderTest, VerifyEncoderDecoderMultipleMetadataReachSizeLimit) {
  MetadataMap metadata_map_empty = {};
  MetadataCallback cb = [](std::unique_ptr<MetadataMap>) -> void {};
  initialize(cb);

  int result = 0;

  for (int i = 0; i < 100; i++) {
    // Cleans up the output buffer.
    memset(output_buffer_.buf, 0, output_buffer_.length);
    output_buffer_.length = 0;

    MetadataMap metadata_map = {
        {"header_key1", std::string(10000, 'a')},
        {"header_key2", std::string(10000, 'b')},
    };
    MetadataMapVec metadata_map_vec;
    metadata_map_vec.push_back(&metadata_map);

    // Encode and decode the second MetadataMap.
    MetadataCallback cb2 = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                     metadata_map_vec, std::placeholders::_1);
    decoder_->callback_ = cb2;
    submitMetadata(metadata_map_vec);

    result = nghttp2_session_mem_recv(session_, output_buffer_.buf, output_buffer_.length);
    if (result < 0) {
      break;
    }
  }
  // Verifies max matadata limit reached.
  EXPECT_LT(result, 0);
  EXPECT_LE(decoder_->max_payload_size_bound_, decoder_->total_payload_size_);

  cleanUp();
}

// Tests encoding/decoding small metadata map vectors.
TEST_F(MetadataEncoderDecoderTest, EncodeMetadataMapVecSmall) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(5, 'a')},
      {"header_key2", std::string(5, 'b')},
  };
  MetadataMap metadata_map_2 = {
      {"header_key3", std::string(5, 'a')},
      {"header_key4", std::string(5, 'b')},
  };
  MetadataMap metadata_map_3 = {
      {"header_key1", std::string(1, 'a')},
      {"header_key2", std::string(1, 'b')},
  };

  MetadataMapVec metadata_map_vec;
  metadata_map_vec.push_back(&metadata_map);
  metadata_map_vec.push_back(&metadata_map_2);
  metadata_map_vec.push_back(&metadata_map_3);

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                  metadata_map_vec, std::placeholders::_1);
  initialize(cb);
  submitMetadata(metadata_map_vec);

  // Verifies flag and payload are encoded correctly.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                           output_buffer_.length - consume_size);

  cleanUp();
}

// Tests encoding/decoding large metadata map vectors.
TEST_F(MetadataEncoderDecoderTest, EncodeMetadataMapVecLarge) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(50000, 'a')},
      {"header_key2", std::string(50000, 'b')},
  };

  MetadataMapVec metadata_map_vec;
  for (int i = 0; i < 10; i++) {
    metadata_map_vec.push_back(&metadata_map);
  }

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                  metadata_map_vec, std::placeholders::_1);
  initialize(cb);
  submitMetadata(metadata_map_vec);

  // Verifies flag and payload are encoded correctly.
  const uint64_t consume_size = random_generator_.random() % output_buffer_.length;
  nghttp2_session_mem_recv(session_, output_buffer_.buf, consume_size);
  nghttp2_session_mem_recv(session_, output_buffer_.buf + consume_size,
                           output_buffer_.length - consume_size);

  cleanUp();
}

TEST_F(MetadataEncoderDecoderTest, TestFrameCountUpperBound) {
  MetadataMap metadata_map = {
      {"header_key1", std::string(5, 'a')},
      {"header_key2", std::string(5, 'b')},
  };

  int size = 10;
  MetadataMapVec metadata_map_vec;
  for (int i = 0; i < size; i++) {
    metadata_map_vec.push_back(&metadata_map);
  }

  // Verifies the encoding/decoding result in decoder's callback functions.
  MetadataCallback cb = std::bind(&MetadataEncoderDecoderTest::verifyMetadataMapVec, this,
                                  metadata_map_vec, std::placeholders::_1);
  initialize(cb);

  encoder_.createPayload(metadata_map_vec);
  EXPECT_LE(size, encoder_.frameCountUpperBound());

  cleanUp();
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
