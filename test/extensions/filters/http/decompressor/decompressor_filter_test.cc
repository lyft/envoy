#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "test/mocks/compression/decompressor/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {
namespace {

class DecompressorFilterTest : public testing::Test {
public:
  DecompressorFilterTest() {}

  void SetUp() override {
    setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");
  }

  // DecompressorFilterTest Helpers
  void setUpFilter(std::string&& yaml) {
    envoy::extensions::filters::http::decompressor::v3::Decompressor decompressor;
    TestUtility::loadFromYaml(yaml, decompressor);
    auto decompressor_factory = std::make_unique<NiceMock<Compression::Decompressor::MockDecompressorFactory>>();
    decompressor_factory_ = decompressor_factory.get();
    config_ = std::make_shared<DecompressorFilterConfig>(decompressor, "test.", stats_,
                                                             runtime_, std::move(decompressor_factory));
    filter_ = std::make_unique<DecompressorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  // void verifyCompressedData() {
  //   EXPECT_EQ(expected_str_.length(),
  //   stats_.counter("test.test.total_uncompressed_bytes").value()); EXPECT_EQ(data_.length(),
  //   stats_.counter("test.test.total_compressed_bytes").value());
  // }

  // void feedBuffer(uint64_t size) {
  //   TestUtility::feedBufferWithRandomCharacters(data_, size);
  //   expected_str_ += data_.toString();
  // }

  // void drainBuffer() {
  //   const uint64_t data_len = data_.length();
  //   data_.drain(data_len);
  // }

  // void doRequest(Http::TestRequestHeaderMapImpl&& headers, bool end_stream) {
  //   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, end_stream));
  // }

  // void doResponseCompression(Http::TestResponseHeaderMapImpl&& headers, bool with_trailers) {
  //   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  //   filter_->setDecoderFilterCallbacks(decoder_callbacks);
  //   uint64_t content_length;
  //   ASSERT_TRUE(absl::SimpleAtoi(headers.get_("content-length"), &content_length));
  //   feedBuffer(content_length);
  //   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  //   EXPECT_EQ("", headers.get_("content-length"));
  //   EXPECT_EQ("test", headers.get_("content-encoding"));
  //   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, !with_trailers));
  //   if (with_trailers) {
  //     Buffer::OwnedImpl trailers_buffer;
  //     EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
  //         .WillOnce(Invoke([&](Buffer::Instance& data, bool) { data_.move(data); }));
  //     Http::TestResponseTrailerMapImpl trailers;
  //     EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
  //   }
  //   verifyCompressedData();
  //   drainBuffer();
  //   EXPECT_EQ(1U, stats_.counter("test.test.compressed").value());
  // }

  // void doResponseNoCompression(Http::TestResponseHeaderMapImpl&& headers) {
  //   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  //   filter_->setDecoderFilterCallbacks(decoder_callbacks);
  //   uint64_t content_length;
  //   ASSERT_TRUE(absl::SimpleAtoi(headers.get_("content-length"), &content_length));
  //   feedBuffer(content_length);
  //   Http::TestResponseHeaderMapImpl continue_headers;
  //   EXPECT_EQ(Http::FilterHeadersStatus::Continue,
  //             filter_->encode100ContinueHeaders(continue_headers));
  //   Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  //   EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  //   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  //   EXPECT_EQ("", headers.get_("content-encoding"));
  //   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
  //   Http::TestResponseTrailerMapImpl trailers;
  //   EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
  //   EXPECT_EQ(1, stats_.counter("test.test.not_compressed").value());
  // }

  Compression::Decompressor::MockDecompressorFactory* decompressor_factory_{};
  DecompressorFilterConfigSharedPtr config_;
  std::unique_ptr<DecompressorFilter> filter_;
  MockBuffer data_;
  std::string expected_str_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(DecompressorFilterTest, ResponseDecompressionActive) {
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).WillOnce(Return(std::move(decompressor)));
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "gzip"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
}

// // Acceptance Testing with default configuration.
// TEST_F(DecompressorFilterTest, AcceptanceTestEncoding) {
//   doRequest({{":method", "get"}, {"accept-encoding", "deflate, test"}}, false);
//   Buffer::OwnedImpl data("hello");
//   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
//   Http::TestRequestTrailerMapImpl trailers;
//   EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
//   doResponseCompression({{":method", "get"}, {"content-length", "256"}}, false);
// }

// TEST_F(DecompressorFilterTest, AcceptanceTestEncodingWithTrailers) {
//   doRequest({{":method", "get"}, {"accept-encoding", "deflate, test"}}, false);
//   Buffer::OwnedImpl data("hello");
//   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
//   Http::TestRequestTrailerMapImpl trailers;
//   EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
//   doResponseCompression({{":method", "get"}, {"content-length", "256"}}, true);
// }

// // Verifies hasCacheControlNoTransform function.
// TEST_F(DecompressorFilterTest, HasCacheControlNoTransform) {
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"cache-control", "no-cache"}};
//     EXPECT_FALSE(hasCacheControlNoTransform(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"cache-control", "no-transform"}};
//     EXPECT_TRUE(hasCacheControlNoTransform(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"cache-control", "No-Transform"}};
//     EXPECT_TRUE(hasCacheControlNoTransform(headers));
//   }
// }

// // Verifies that compression is skipped when cache-control header has no-transform value.
// TEST_F(DecompressorFilterTest, HasCacheControlNoTransformNoCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test;q=1, deflate"}}, true);
//   doResponseNoCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"cache-control", "no-transform"}});
// }

// // Verifies that compression is NOT skipped when cache-control header does NOT have no-transform
// // value.
// TEST_F(DecompressorFilterTest, HasCacheControlNoTransformCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test, deflate"}}, true);
//   doResponseCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"cache-control", "no-cache"}}, false);
// }

// TEST_F(DecompressorFilterTest, NoAcceptEncodingHeader) {
//   doRequest({{":method", "get"}, {}}, true);
//   doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
//   EXPECT_EQ(1, stats_.counter("test.test.no_accept_header").value());
// }

// // Verifies isAcceptEncodingAllowed function.
// TEST_F(DecompressorFilterTest, IsAcceptEncodingAllowed) {
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("deflate, test, br"));
//     EXPECT_EQ(1, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("deflate, test;q=1.0, *;q=0.5"));
//     EXPECT_EQ(2, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("\tdeflate\t, test\t ; q\t =\t 1.0,\t * ;q=0.5"));
//     EXPECT_EQ(3, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("deflate,test;q=1.0,*;q=0"));
//     EXPECT_EQ(4, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("deflate, test;q=0.2, br;q=1"));
//     EXPECT_EQ(5, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("*"));
//     EXPECT_EQ(1, stats_.counter("test.test.header_wildcard").value());
//     EXPECT_EQ(5, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("*;q=1"));
//     EXPECT_EQ(2, stats_.counter("test.test.header_wildcard").value());
//     EXPECT_EQ(5, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     // test header is not valid due to q=0.
//     EXPECT_FALSE(isAcceptEncodingAllowed("test;q=0,*;q=1"));
//     EXPECT_EQ(5, stats_.counter("test.test.header_compressor_used").value());
//     EXPECT_EQ(1, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity, *;q=0"));
//     EXPECT_EQ(1, stats_.counter("test.test.header_identity").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity;q=0.5, *;q=0"));
//     EXPECT_EQ(2, stats_.counter("test.test.header_identity").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity;q=0, *;q=0"));
//     EXPECT_EQ(2, stats_.counter("test.test.header_identity").value());
//     EXPECT_EQ(2, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("xyz;q=1, br;q=0.2, *"));
//     EXPECT_EQ(3, stats_.counter("test.test.header_wildcard").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("xyz;q=1, br;q=0.2, *;q=0"));
//     EXPECT_EQ(3, stats_.counter("test.test.header_wildcard").value());
//     EXPECT_EQ(3, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("xyz;q=1, br;q=0.2"));
//     EXPECT_EQ(4, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity"));
//     EXPECT_EQ(3, stats_.counter("test.test.header_identity").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity;q=1"));
//     EXPECT_EQ(4, stats_.counter("test.test.header_identity").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity;q=0"));
//     EXPECT_EQ(4, stats_.counter("test.test.header_identity").value());
//     EXPECT_EQ(5, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     // Test that we return identity and ignore the invalid wildcard.
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity, *;q=0"));
//     EXPECT_EQ(5, stats_.counter("test.test.header_identity").value());
//     EXPECT_EQ(5, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_TRUE(isAcceptEncodingAllowed("deflate, test;Q=.5, br"));
//     EXPECT_EQ(6, stats_.counter("test.test.header_compressor_used").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("identity;Q=0"));
//     EXPECT_EQ(5, stats_.counter("test.test.header_identity").value());
//     EXPECT_EQ(6, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed(""));
//     EXPECT_EQ(5, stats_.counter("test.test.header_identity").value());
//     EXPECT_EQ(7, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     // Compressor "test2" from an independent filter chain should not overshadow "test".
//     // The independence is simulated with a new instance DecoderFilterCallbacks set for "test2".
//     Stats::TestUtil::TestStore stats;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "test2");
//     std::unique_ptr<CompressorFilter> filter2 = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     filter2->setDecoderFilterCallbacks(decoder_callbacks);

//     EXPECT_TRUE(isAcceptEncodingAllowed("test;Q=.5,test2;q=0.75"));
//     EXPECT_TRUE(isAcceptEncodingAllowed("test;Q=.5,test2;q=0.75", filter2));
//     EXPECT_EQ(0, stats_.counter("test.test.header_compressor_overshadowed").value());
//     EXPECT_EQ(7, stats_.counter("test.test.header_compressor_used").value());
//     EXPECT_EQ(1, stats.counter("test2.test2.header_compressor_used").value());
//   }
//   {
//     EXPECT_FALSE(isAcceptEncodingAllowed("test;q=invalid"));
//     EXPECT_EQ(8, stats_.counter("test.test.header_not_valid").value());
//   }
//   {
//     // check if the legacy "header_gzip" counter is incremented for gzip compression filter
//     Stats::TestUtil::TestStore stats;
//     ;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "gzip");
//     std::unique_ptr<CompressorFilter> gzip_filter = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     gzip_filter->setDecoderFilterCallbacks(decoder_callbacks);

//     EXPECT_TRUE(isAcceptEncodingAllowed("gzip;q=0.75", gzip_filter));
//     EXPECT_EQ(1, stats.counter("test2.gzip.header_gzip").value());
//     // This fake Accept-Encoding is ignored as a cached decision is used.
//     EXPECT_TRUE(isAcceptEncodingAllowed("fake", gzip_filter));
//     EXPECT_EQ(2, stats.counter("test2.gzip.header_gzip").value());
//   }
//   {
//     // check if identity stat is increased twice (the second time via the cached path).
//     Stats::TestUtil::TestStore stats;
//     ;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "test");
//     std::unique_ptr<CompressorFilter> filter2 = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     filter2->setDecoderFilterCallbacks(decoder_callbacks);

//     EXPECT_FALSE(isAcceptEncodingAllowed("identity", filter2));
//     EXPECT_EQ(1, stats.counter("test2.test.header_identity").value());
//     // This fake Accept-Encoding is ignored as a cached decision is used.
//     EXPECT_FALSE(isAcceptEncodingAllowed("fake", filter2));
//     EXPECT_EQ(2, stats.counter("test2.test.header_identity").value());
//   }
//   {
//     // check if not_valid stat is increased twice (the second time via the cached path).
//     Stats::TestUtil::TestStore stats;
//     ;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "test");
//     std::unique_ptr<CompressorFilter> filter2 = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     filter2->setDecoderFilterCallbacks(decoder_callbacks);

//     EXPECT_FALSE(isAcceptEncodingAllowed("test;q=invalid", filter2));
//     EXPECT_EQ(1, stats.counter("test2.test.header_not_valid").value());
//     // This fake Accept-Encoding is ignored as a cached decision is used.
//     EXPECT_FALSE(isAcceptEncodingAllowed("fake", filter2));
//     EXPECT_EQ(2, stats.counter("test2.test.header_not_valid").value());
//   }
//   {
//     // Test that encoding decision is cached when used by multiple filters.
//     Stats::TestUtil::TestStore stats;
//     ;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config1;
//     config1 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test1.", stats, runtime,
//         "test1");
//     std::unique_ptr<CompressorFilter> filter1 = std::make_unique<CompressorFilter>(config1);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "test2");
//     std::unique_ptr<CompressorFilter> filter2 = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     filter1->setDecoderFilterCallbacks(decoder_callbacks);
//     filter2->setDecoderFilterCallbacks(decoder_callbacks);

//     std::string accept_encoding = "test1;Q=.5,test2;q=0.75";
//     EXPECT_FALSE(isAcceptEncodingAllowed(accept_encoding, filter1));
//     EXPECT_TRUE(isAcceptEncodingAllowed(accept_encoding, filter2));
//     EXPECT_EQ(1, stats.counter("test1.test1.header_compressor_overshadowed").value());
//     EXPECT_EQ(1, stats.counter("test2.test2.header_compressor_used").value());
//     EXPECT_FALSE(isAcceptEncodingAllowed(accept_encoding, filter1));
//     EXPECT_EQ(2, stats.counter("test1.test1.header_compressor_overshadowed").value());
//     // These fake Accept-Encoding header is ignored. Instead the cached decision is used.
//     EXPECT_TRUE(isAcceptEncodingAllowed("fake", filter2));
//     EXPECT_EQ(2, stats.counter("test2.test2.header_compressor_used").value());
//   }
//   {
//     // Test that first registered filter is used when handling wildcard.
//     Stats::TestUtil::TestStore stats;
//     ;
//     NiceMock<Runtime::MockLoader> runtime;
//     envoy::extensions::filters::http::compressor::v3::Compressor compressor;
//     TestUtility::loadFromJson(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF",
//                               compressor);
//     CompressorFilterConfigSharedPtr config1;
//     config1 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test1.", stats, runtime,
//         "test1");
//     std::unique_ptr<CompressorFilter> filter1 = std::make_unique<CompressorFilter>(config1);
//     CompressorFilterConfigSharedPtr config2;
//     config2 =
//         std::make_shared<MockCompressorFilterConfig>(compressor, "test2.", stats, runtime,
//         "test2");
//     std::unique_ptr<CompressorFilter> filter2 = std::make_unique<CompressorFilter>(config2);
//     NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//     filter1->setDecoderFilterCallbacks(decoder_callbacks);
//     filter2->setDecoderFilterCallbacks(decoder_callbacks);

//     std::string accept_encoding = "*";
//     EXPECT_TRUE(isAcceptEncodingAllowed(accept_encoding, filter1));
//     EXPECT_FALSE(isAcceptEncodingAllowed(accept_encoding, filter2));
//     EXPECT_EQ(1, stats.counter("test1.test1.header_wildcard").value());
//     EXPECT_EQ(1, stats.counter("test2.test2.header_wildcard").value());
//   }
// }

// // Verifies that compression is skipped when accept-encoding header is not allowed.
// TEST_F(DecompressorFilterTest, AcceptEncodingNoCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test;q=0, deflate"}}, true);
//   doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
// }

// // Verifies that compression is NOT skipped when accept-encoding header is allowed.
// TEST_F(DecompressorFilterTest, AcceptEncodingCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test, deflate"}}, true);
//   doResponseCompression({{":method", "get"}, {"content-length", "256"}}, false);
// }

// // Verifies isMinimumContentLength function.
// TEST_F(DecompressorFilterTest, IsMinimumContentLength) {
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-length", "31"}};
//     EXPECT_TRUE(isMinimumContentLength(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-length", "29"}};
//     EXPECT_FALSE(isMinimumContentLength(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
//     EXPECT_TRUE(isMinimumContentLength(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "Chunked"}};
//     EXPECT_TRUE(isMinimumContentLength(headers));
//   }

//   setUpFilter(R"EOF(
// {
//   "content_length": 500,
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-length", "501"}};
//     EXPECT_TRUE(isMinimumContentLength(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
//     EXPECT_TRUE(isMinimumContentLength(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-length", "499"}};
//     EXPECT_FALSE(isMinimumContentLength(headers));
//   }
// }

// // Verifies that compression is skipped when content-length header is NOT allowed.
// TEST_F(DecompressorFilterTest, ContentLengthNoCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseNoCompression({{":method", "get"}, {"content-length", "10"}});
// }

// // Verifies that compression is NOT skipped when content-length header is allowed.
// TEST_F(DecompressorFilterTest, ContentLengthCompression) {
//   setUpFilter(R"EOF(
// {
//   "content_length": 500,
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseCompression({{":method", "get"}, {"content-length", "1000"}}, false);
// }

// // Verifies isContentTypeAllowed function.
// TEST_F(DecompressorFilterTest, IsContentTypeAllowed) {

//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "text/html"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "text/xml"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "text/plain"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "application/javascript"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "image/svg+xml"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type",
//     "application/json;charset=utf-8"}}; EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "application/json"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "application/xhtml+xml"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "Application/XHTML+XML"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "image/jpeg"}};
//     EXPECT_FALSE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "\ttext/html\t"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }

//   setUpFilter(R"EOF(
//     {
//       "content_type": [
//         "text/html",
//         "xyz/svg+xml",
//         "Test/INSENSITIVE"
//       ],
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
//     }
//   )EOF");

//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "xyz/svg+xml"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "xyz/false"}};
//     EXPECT_FALSE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "image/jpeg"}};
//     EXPECT_FALSE(isContentTypeAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"content-type", "test/insensitive"}};
//     EXPECT_TRUE(isContentTypeAllowed(headers));
//   }
// }

// // Verifies that compression is skipped when content-type header is NOT allowed.
// TEST_F(DecompressorFilterTest, ContentTypeNoCompression) {
//   setUpFilter(R"EOF(
//     {
//       "content_type": [
//         "text/html",
//         "text/css",
//         "text/plain",
//         "application/javascript",
//         "application/json",
//         "font/eot",
//         "image/svg+xml"
//       ],
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
//     }
//   )EOF");
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseNoCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"content-type", "image/jpeg"}});
//   EXPECT_EQ(1, stats_.counter("test.test.header_not_valid").value());
// }

// // Verifies that compression is NOT skipped when content-encoding header is allowed.
// TEST_F(DecompressorFilterTest, ContentTypeCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseCompression({{":method", "get"},
//                          {"content-length", "256"},
//                          {"content-type", "application/json;charset=utf-8"}},
//                         false);
// }

// // Verifies sanitizeEtagHeader function.
// TEST_F(DecompressorFilterTest, SanitizeEtagHeader) {
//   {
//     std::string etag_header{R"EOF(W/"686897696a7c876b7e")EOF"};
//     Http::TestResponseHeaderMapImpl headers = {{"etag", etag_header}};
//     sanitizeEtagHeader(headers);
//     EXPECT_EQ(etag_header, headers.get_("etag"));
//   }
//   {
//     std::string etag_header{R"EOF(w/"686897696a7c876b7e")EOF"};
//     Http::TestResponseHeaderMapImpl headers = {{"etag", etag_header}};
//     sanitizeEtagHeader(headers);
//     EXPECT_EQ(etag_header, headers.get_("etag"));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"etag", "686897696a7c876b7e"}};
//     sanitizeEtagHeader(headers);
//     EXPECT_FALSE(headers.has("etag"));
//   }
// }

// // Verifies isEtagAllowed function.
// TEST_F(DecompressorFilterTest, IsEtagAllowed) {
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"etag", R"EOF(W/"686897696a7c876b7e")EOF"}};
//     EXPECT_TRUE(isEtagAllowed(headers));
//     EXPECT_EQ(0, stats_.counter("test.test.not_compressed_etag").value());
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"etag", "686897696a7c876b7e"}};
//     EXPECT_TRUE(isEtagAllowed(headers));
//     EXPECT_EQ(0, stats_.counter("test.test.not_compressed_etag").value());
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     EXPECT_TRUE(isEtagAllowed(headers));
//     EXPECT_EQ(0, stats_.counter("test.test.not_compressed_etag").value());
//   }

//   setUpFilter(R"EOF(
// {
//   "disable_on_etag_header": true,
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"etag", R"EOF(W/"686897696a7c876b7e")EOF"}};
//     EXPECT_FALSE(isEtagAllowed(headers));
//     EXPECT_EQ(1, stats_.counter("test.test.not_compressed_etag").value());
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"etag", "686897696a7c876b7e"}};
//     EXPECT_FALSE(isEtagAllowed(headers));
//     EXPECT_EQ(2, stats_.counter("test.test.not_compressed_etag").value());
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     EXPECT_TRUE(isEtagAllowed(headers));
//     EXPECT_EQ(2, stats_.counter("test.test.not_compressed_etag").value());
//   }
// }

// // Verifies that compression is skipped when etag header is NOT allowed.
// TEST_F(DecompressorFilterTest, EtagNoCompression) {
//   setUpFilter(R"EOF(
// {
//   "disable_on_etag_header": true,
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseNoCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"etag",
//       R"EOF(W/"686897696a7c876b7e")EOF"}});
//   EXPECT_EQ(1, stats_.counter("test.test.not_compressed_etag").value());
// }

// // Verifies that compression is skipped when etag header is NOT allowed.
// TEST_F(DecompressorFilterTest, EtagCompression) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   Http::TestResponseHeaderMapImpl headers{
//       {":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}};
//   feedBuffer(256);
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
//   EXPECT_FALSE(headers.has("etag"));
//   EXPECT_EQ("test", headers.get_("content-encoding"));
// }

// // Verifies isTransferEncodingAllowed function.
// TEST_F(DecompressorFilterTest, IsTransferEncodingAllowed) {
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     EXPECT_TRUE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
//     EXPECT_TRUE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "Chunked"}};
//     EXPECT_TRUE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "deflate"}};
//     EXPECT_FALSE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "Deflate"}};
//     EXPECT_FALSE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "test"}};
//     EXPECT_FALSE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", "test, chunked"}};
//     EXPECT_FALSE(isTransferEncodingAllowed(headers));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"transfer-encoding", " test\t,  chunked\t"}};
//     EXPECT_FALSE(isTransferEncodingAllowed(headers));
//   }
// }

// // Tests compression when Transfer-Encoding header exists.
// TEST_F(DecompressorFilterTest, TransferEncodingChunked) {
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked"}}, false);
// }

// // Tests compression when Transfer-Encoding header exists.
// TEST_F(DecompressorFilterTest, AcceptanceTransferEncoding) {

//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   doResponseNoCompression(
//       {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked,
//       deflate"}});
// }

// // Content-Encoding: upstream response is already encoded.
// TEST_F(DecompressorFilterTest, ContentEncodingAlreadyEncoded) {
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   Http::TestResponseHeaderMapImpl response_headers{
//       {":method", "get"}, {"content-length", "256"}, {"content-encoding", "deflate, gzip"}};
//   feedBuffer(256);
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers,
//   false)); EXPECT_TRUE(response_headers.has("content-length"));
//   EXPECT_FALSE(response_headers.has("transfer-encoding"));
//   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
// }

// // No compression when upstream response is empty.
// TEST_F(DecompressorFilterTest, EmptyResponse) {

//   Http::TestResponseHeaderMapImpl headers{{":method", "get"}, {":status", "204"}};
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
//   EXPECT_EQ("", headers.get_("content-length"));
//   EXPECT_EQ("", headers.get_("content-encoding"));
//   EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, true));
// }

// // Verifies insertVaryHeader function.
// TEST_F(DecompressorFilterTest, InsertVaryHeader) {
//   {
//     Http::TestResponseHeaderMapImpl headers = {};
//     insertVaryHeader(headers);
//     EXPECT_EQ("Accept-Encoding", headers.get_("vary"));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"vary", "Cookie"}};
//     insertVaryHeader(headers);
//     EXPECT_EQ("Cookie, Accept-Encoding", headers.get_("vary"));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"vary", "accept-encoding"}};
//     insertVaryHeader(headers);
//     EXPECT_EQ("accept-encoding, Accept-Encoding", headers.get_("vary"));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"vary", "Accept-Encoding, Cookie"}};
//     insertVaryHeader(headers);
//     EXPECT_EQ("Accept-Encoding, Cookie", headers.get_("vary"));
//   }
//   {
//     Http::TestResponseHeaderMapImpl headers = {{"vary", "Accept-Encoding"}};
//     insertVaryHeader(headers);
//     EXPECT_EQ("Accept-Encoding", headers.get_("vary"));
//   }
// }

// // Filter should set Vary header value with `accept-encoding`.
// TEST_F(DecompressorFilterTest, NoVaryHeader) {
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   Http::TestResponseHeaderMapImpl headers{{":method", "get"}, {"content-length", "256"}};
//   feedBuffer(256);
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
//   EXPECT_TRUE(headers.has("vary"));
//   EXPECT_EQ("Accept-Encoding", headers.get_("vary"));
// }

// // Filter should set Vary header value with `accept-encoding` and preserve other values.
// TEST_F(DecompressorFilterTest, VaryOtherValues) {
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   Http::TestResponseHeaderMapImpl headers{
//       {":method", "get"}, {"content-length", "256"}, {"vary", "User-Agent, Cookie"}};
//   feedBuffer(256);
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
//   EXPECT_TRUE(headers.has("vary"));
//   EXPECT_EQ("User-Agent, Cookie, Accept-Encoding", headers.get_("vary"));
// }

// // Vary header should have only one `accept-encoding`value.
// TEST_F(DecompressorFilterTest, VaryAlreadyHasAcceptEncoding) {
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   doRequest({{":method", "get"}, {"accept-encoding", "test"}}, true);
//   Http::TestResponseHeaderMapImpl headers{
//       {":method", "get"}, {"content-length", "256"}, {"vary", "accept-encoding"}};
//   feedBuffer(256);
//   EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
//   EXPECT_TRUE(headers.has("vary"));
//   EXPECT_EQ("accept-encoding, Accept-Encoding", headers.get_("vary"));
// }

// // Verify removeAcceptEncoding header.
// TEST_F(DecompressorFilterTest, RemoveAcceptEncodingHeader) {
//   NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
//   filter_->setDecoderFilterCallbacks(decoder_callbacks);
//   {
//     Http::TestRequestHeaderMapImpl headers = {{"accept-encoding", "deflate, test, gzip, br"}};
//     setUpFilter(R"EOF(
// {
//   "remove_accept_encoding_header": true,
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//     EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
//     EXPECT_FALSE(headers.has("accept-encoding"));
//   }
//   {
//     Http::TestRequestHeaderMapImpl headers = {{"accept-encoding", "deflate, test, gzip, br"}};
//     setUpFilter(R"EOF(
// {
//   "compressor_library": {
//      "typed_config": {
//        "@type": "type.googleapis.com/envoy.extensions.filters.http.compressor.gzip.v3.Gzip"
//      }
//   }
// }
// )EOF");
//     EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
//     EXPECT_TRUE(headers.has("accept-encoding"));
//     EXPECT_EQ("deflate, test, gzip, br", headers.get_("accept-encoding"));
//   }
// }

} // namespace
} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
