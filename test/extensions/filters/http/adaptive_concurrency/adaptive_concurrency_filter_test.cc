#include <chrono>

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace {

class MockConcurrencyController : public ConcurrencyController::ConcurrencyController {
public:
  MOCK_METHOD0(tryForwardRequest, bool());
  MOCK_METHOD1(recordLatencySample, void(const std::chrono::nanoseconds&));
};

class AdaptiveConcurrencyFilterTest : public testing::Test {
public:
  void SetupTest();

  std::unique_ptr<AdaptiveConcurrencyFilter> filter_;
  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::shared_ptr<MockConcurrencyController> controller_{new MockConcurrencyController()};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

void AdaptiveConcurrencyFilterTest::SetupTest() {
  filter_.reset();

  const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency config;
  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);

  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
}

// Verify the parts of the filter that aren't doing the work don't return
// anything unexpected.
TEST_F(AdaptiveConcurrencyFilterTest, UnusedFuncsTest) {
  SetupTest();

  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, true));

  Http::TestHeaderMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers));

  Buffer::OwnedImpl response_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, false));

  Http::TestHeaderMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));

  Http::MetadataMap metadata;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata));
}

TEST_F(AdaptiveConcurrencyFilterTest, DecodeHeadersTest) {
  SetupTest();

  Http::TestHeaderMapImpl request_headers;

  EXPECT_CALL(*controller_, tryForwardRequest()).Times(1).WillRepeatedly(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(*controller_, tryForwardRequest()).Times(1).WillRepeatedly(Return(false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _))
      .Times(1);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdaptiveConcurrencyFilterTest, EncodeHeadersTest) {
  SetupTest();

  // Get the filter to record the request start time via decode.
  Http::TestHeaderMapImpl request_headers;
  EXPECT_CALL(*controller_, tryForwardRequest()).Times(1).WillRepeatedly(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  const std::chrono::nanoseconds advance_time = std::chrono::milliseconds(42);
  const auto mt = time_system_.monotonicTime();
  time_system_.setMonotonicTime(mt + advance_time);

  Http::TestHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_CALL(*controller_, recordLatencySample(advance_time)).Times(1);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

} // namespace
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
