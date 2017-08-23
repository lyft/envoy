#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/base64.h"
#include "common/common/utility.h"
#include "common/grpc/grpc_web_filter.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::Values;
using testing::_;

namespace Envoy {
namespace Grpc {
namespace {
const char MESSAGE[] = "\x00\x00\x00\x00\x11grpc-web-bin-data";
const size_t MESSAGE_SIZE = sizeof(MESSAGE) - 1;
const char TEXT_MESSAGE[] = "\x00\x00\x00\x00\x12grpc-web-text-data";
const size_t TEXT_MESSAGE_SIZE = sizeof(TEXT_MESSAGE) - 1;
const char B64_MESSAGE[] = "AAAAABJncnBjLXdlYi10ZXh0LWRhdGE=";
const size_t B64_MESSAGE_SIZE = sizeof(B64_MESSAGE) - 1;
const char INVALID_B64_MESSAGE[] = "****";
const size_t INVALID_B64_MESSAGE_SIZE = sizeof(INVALID_B64_MESSAGE) - 1;
const char TRAILERS[] = "\x80\x00\x00\x00\x20grpc-status:0\r\ngrpc-message:ok\r\n";
const size_t TRAILERS_SIZE = sizeof(TRAILERS) - 1;
} // namespace

class GrpcWebFilterTest : public testing::TestWithParam<std::tuple<std::string, std::string>> {
public:
  GrpcWebFilterTest() : filter_(cm_) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ~GrpcWebFilterTest() override { filter_.onDestroy(); }

  const std::string& request_content_type() const { return std::get<0>(GetParam()); }

  const std::string& request_accept() const { return std::get<1>(GetParam()); }

  bool isTextRequest() const {
    return request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebText ||
           request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebTextProto;
  }

  bool isBinaryRequest() const {
    return request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWeb ||
           request_content_type() == Http::Headers::get().ContentTypeValues.GrpcWebProto;
  }

  bool accept_text_response() const {
    return request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebText ||
           request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebTextProto;
  }

  bool accept_binary_response() const {
    return request_accept() == Http::Headers::get().ContentTypeValues.GrpcWeb ||
           request_accept() == Http::Headers::get().ContentTypeValues.GrpcWebProto;
  }

  bool doStatTracking() const { return filter_.do_stat_tracking_; }

  GrpcWebFilter filter_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(GrpcWebFilterTest, SupportedContentTypes) {
  const std::string supported_content_types[] = {
      Http::Headers::get().ContentTypeValues.GrpcWeb,
      Http::Headers::get().ContentTypeValues.GrpcWebProto,
      Http::Headers::get().ContentTypeValues.GrpcWebText,
      Http::Headers::get().ContentTypeValues.GrpcWebTextProto};
  for (auto& content_type : supported_content_types) {
    Http::TestHeaderMapImpl request_headers;
    request_headers.addCopy(Http::Headers::get().ContentType, content_type);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
              request_headers.ContentType()->value().c_str());
  }
}

TEST_F(GrpcWebFilterTest, UnsupportedContentType) {
  Http::TestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::Headers::get().ContentType, "unsupported");
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool) {
        uint64_t code;
        StringUtil::atoul(headers.Status()->value().c_str(), code);
        EXPECT_EQ(static_cast<uint64_t>(Http::Code::UnsupportedMediaType), code);
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
}

TEST_F(GrpcWebFilterTest, NoContentType) {
  Http::TestHeaderMapImpl request_headers;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool) {
        uint64_t code;
        StringUtil::atoul(headers.Status()->value().c_str(), code);
        EXPECT_EQ(static_cast<uint64_t>(Http::Code::UnsupportedMediaType), code);
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers, false));
}

TEST_F(GrpcWebFilterTest, InvalidBase64) {
  Http::TestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::Headers::get().ContentType,
                          Http::Headers::get().ContentTypeValues.GrpcWebText);
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool) {
        uint64_t code;
        StringUtil::atoul(headers.Status()->value().c_str(), code);
        EXPECT_EQ(static_cast<uint64_t>(Http::Code::BadRequest), code);
      }));
  EXPECT_CALL(decoder_callbacks_, encodeData(_, _))
      .WillOnce(Invoke([](Buffer::Instance& data, bool) {
        EXPECT_EQ("Bad gRPC-web request, invalid base64 data.", TestUtility::bufferToString(data));
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            request_headers.ContentType()->value().c_str());
  EXPECT_EQ(Http::Headers::get().TEValues.Trailers, request_headers.TE()->value().c_str());
  EXPECT_EQ(Http::Headers::get().GrpcAcceptEncodingValues.Default,
            request_headers.GrpcAcceptEncoding()->value().c_str());

  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl decoded_buffer;
  request_buffer.add(&INVALID_B64_MESSAGE, INVALID_B64_MESSAGE_SIZE);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_.decodeData(request_buffer, true));
}

TEST_P(GrpcWebFilterTest, StatsNoCluster) {
  Http::TestHeaderMapImpl request_headers{{"content-type", request_content_type()},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_CALL(cm_, get(_)).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_FALSE(doStatTracking());
}

TEST_P(GrpcWebFilterTest, StatsNormalResponse) {
  Http::TestHeaderMapImpl request_headers{{"content-type", request_content_type()},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc-web.lyft.users.BadCompanions.GetBadCompanions.success")
                     .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc-web.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_P(GrpcWebFilterTest, StatsErrorResponse) {
  Http::TestHeaderMapImpl request_headers{{"content-type", request_content_type()},
                                          {":path", "/lyft.users.BadCompanions/GetBadCompanions"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(data, false));
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "1"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc-web.lyft.users.BadCompanions.GetBadCompanions.failure")
                     .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("grpc-web.lyft.users.BadCompanions.GetBadCompanions.total")
                     .value());
}

TEST_P(GrpcWebFilterTest, Unary) {
  // Tests request headers.
  Http::TestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::Headers::get().ContentType, request_content_type());
  request_headers.addCopy(Http::Headers::get().Accept, request_accept());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
            request_headers.ContentType()->value().c_str());
  EXPECT_EQ(Http::Headers::get().TEValues.Trailers, request_headers.TE()->value().c_str());
  EXPECT_EQ(Http::Headers::get().GrpcAcceptEncodingValues.Default,
            request_headers.GrpcAcceptEncoding()->value().c_str());

  // Tests request data.
  if (isBinaryRequest()) {
    Buffer::OwnedImpl request_buffer;
    Buffer::OwnedImpl decoded_buffer;
    for (size_t i = 0; i < MESSAGE_SIZE; i++) {
      request_buffer.add(&MESSAGE[i], 1);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
      decoded_buffer.move(request_buffer);
    }
    EXPECT_EQ(std::string(MESSAGE, MESSAGE_SIZE), TestUtility::bufferToString(decoded_buffer));
  } else if (isTextRequest()) {
    Buffer::OwnedImpl request_buffer;
    Buffer::OwnedImpl decoded_buffer;
    for (size_t i = 0; i < B64_MESSAGE_SIZE; i++) {
      request_buffer.add(&B64_MESSAGE[i], 1);
      if (i % 4 == 3) {
        EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.decodeData(request_buffer, true));
      } else {
        EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
                  filter_.decodeData(request_buffer, true));
      }
      decoded_buffer.move(request_buffer);
    }
    EXPECT_EQ(std::string(TEXT_MESSAGE, TEXT_MESSAGE_SIZE),
              TestUtility::bufferToString(decoded_buffer));
  } else {
    FAIL() << "Unsupported gRPC-Web request content-type: " << request_content_type();
  }

  // Tests request trailers, they are passed through.
  Http::TestHeaderMapImpl request_trailers;
  request_trailers.addCopy(Http::Headers::get().GrpcStatus, "0");
  request_trailers.addCopy(Http::Headers::get().GrpcMessage, "ok");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.decodeTrailers(request_trailers));
  EXPECT_STREQ("0", request_trailers.GrpcStatus()->value().c_str());
  EXPECT_STREQ("ok", request_trailers.GrpcMessage()->value().c_str());

  // Tests response headers.
  Http::TestHeaderMapImpl response_headers;
  response_headers.addCopy(Http::Headers::get().Status, "200");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_.encodeHeaders(response_headers, false));
  EXPECT_EQ("200", response_headers.get_(Http::Headers::get().Status.get()));
  if (accept_binary_response()) {
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWebProto,
              response_headers.ContentType()->value().c_str());
  } else if (accept_text_response()) {
    EXPECT_EQ(Http::Headers::get().ContentTypeValues.GrpcWebTextProto,
              response_headers.ContentType()->value().c_str());
  } else {
    FAIL() << "Unsupported gRPC-Web request accept: " << request_accept();
  }

  // Tests response data.
  if (accept_binary_response()) {
    Buffer::OwnedImpl response_buffer;
    Buffer::OwnedImpl encoded_buffer;
    for (size_t i = 0; i < MESSAGE_SIZE; i++) {
      response_buffer.add(&MESSAGE[i], 1);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
      encoded_buffer.move(response_buffer);
    }
    EXPECT_EQ(std::string(MESSAGE, MESSAGE_SIZE), TestUtility::bufferToString(encoded_buffer));
  } else if (accept_text_response()) {
    Buffer::OwnedImpl response_buffer;
    Buffer::OwnedImpl encoded_buffer;
    for (size_t i = 0; i < TEXT_MESSAGE_SIZE; i++) {
      response_buffer.add(&TEXT_MESSAGE[i], 1);
      if (i < TEXT_MESSAGE_SIZE - 1) {
        EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
                  filter_.encodeData(response_buffer, false));
      } else {
        EXPECT_EQ(Http::FilterDataStatus::Continue, filter_.encodeData(response_buffer, false));
      }
      encoded_buffer.move(response_buffer);
    }
    EXPECT_EQ(std::string(B64_MESSAGE, B64_MESSAGE_SIZE),
              TestUtility::bufferToString(encoded_buffer));
  } else {
    FAIL() << "Unsupported gRPC-Web response content-type: "
           << response_headers.ContentType()->value().c_str();
  }

  // Tests response trailers.
  Buffer::OwnedImpl trailers_buffer;
  EXPECT_CALL(encoder_callbacks_, addEncodedData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    trailers_buffer.move(data);
  }));
  Http::TestHeaderMapImpl response_trailers;
  response_trailers.addCopy(Http::Headers::get().GrpcStatus, "0");
  response_trailers.addCopy(Http::Headers::get().GrpcMessage, "ok");
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_.encodeTrailers(response_trailers));
  if (accept_binary_response()) {
    EXPECT_EQ(std::string(TRAILERS, TRAILERS_SIZE), TestUtility::bufferToString(trailers_buffer));
  } else if (accept_text_response()) {
    EXPECT_EQ(std::string(TRAILERS, TRAILERS_SIZE),
              Base64::decode(TestUtility::bufferToString(trailers_buffer)));
  } else {
    FAIL() << "Unsupported gRPC-Web response content-type: "
           << response_headers.ContentType()->value().c_str();
  }
}

INSTANTIATE_TEST_CASE_P(Unary, GrpcWebFilterTest,
                        Combine(Values(Http::Headers::get().ContentTypeValues.GrpcWeb,
                                       Http::Headers::get().ContentTypeValues.GrpcWebProto,
                                       Http::Headers::get().ContentTypeValues.GrpcWebText,
                                       Http::Headers::get().ContentTypeValues.GrpcWebTextProto),
                                Values(Http::Headers::get().ContentTypeValues.GrpcWeb,
                                       Http::Headers::get().ContentTypeValues.GrpcWebProto,
                                       Http::Headers::get().ContentTypeValues.GrpcWebText,
                                       Http::Headers::get().ContentTypeValues.GrpcWebTextProto)));

} // namespace Grpc
} // namespace Envoy
