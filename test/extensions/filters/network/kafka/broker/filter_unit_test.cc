#include "extensions/filters/network/kafka/broker/filter.h"
#include "extensions/filters/network/kafka/external/requests.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// Mocks.

class MockKafkaMetricsFacade : public KafkaMetricsFacade {
public:
  MOCK_METHOD1(onMessage, void(AbstractRequestSharedPtr));
  MOCK_METHOD1(onMessage, void(AbstractResponseSharedPtr));
  MOCK_METHOD1(onFailedParse, void(RequestParseFailureSharedPtr));
  MOCK_METHOD1(onFailedParse, void(ResponseMetadataSharedPtr));
  MOCK_METHOD0(onRequestException, void());
  MOCK_METHOD0(onResponseException, void());
};

using MockKafkaMetricsFacadeSharedPtr = std::shared_ptr<MockKafkaMetricsFacade>;

class MockResponseDecoder : public ResponseDecoder {
public:
  MockResponseDecoder() : ResponseDecoder{{}} {};
  MOCK_METHOD1(onData, void(Buffer::Instance&));
  MOCK_METHOD3(expectResponse, void(const int32_t, const int16_t, const int16_t));
  MOCK_METHOD0(reset, void());
};

using MockResponseDecoderSharedPtr = std::shared_ptr<MockResponseDecoder>;

class MockRequestDecoder : public RequestDecoder {
public:
  MockRequestDecoder() : RequestDecoder{{}} {};
  MOCK_METHOD1(onData, void(Buffer::Instance&));
  MOCK_METHOD0(reset, void());
};

using MockRequestDecoderSharedPtr = std::shared_ptr<MockRequestDecoder>;

class MockTimeSource : public TimeSource {
public:
  MOCK_METHOD0(systemTime, SystemTime());
  MOCK_METHOD0(monotonicTime, MonotonicTime());
};

class MockRichRequestMetrics : public RichRequestMetrics {
public:
  MOCK_METHOD1(onRequest, void(const int16_t));
  MOCK_METHOD0(onUnknownRequest, void());
  MOCK_METHOD0(onBrokenRequest, void());
};

class MockRichResponseMetrics : public RichResponseMetrics {
public:
  MOCK_METHOD2(onResponse, void(const int16_t, const long long duration));
  MOCK_METHOD0(onUnknownResponse, void());
  MOCK_METHOD0(onBrokenResponse, void());
};

class MockRequest : public AbstractRequest {
public:
  MockRequest(const int16_t api_key, const int16_t api_version, const int32_t correlation_id)
      : AbstractRequest{{api_key, api_version, correlation_id, ""}} {};
  uint32_t computeSize() const override { return 0; };
  uint32_t encode(Buffer::Instance&) const override { return 0; };
};

class MockResponse : public AbstractResponse {
public:
  MockResponse(const int16_t api_key, const int32_t correlation_id)
      : AbstractResponse{{api_key, 0, correlation_id}} {};
  uint32_t computeSize() const override { return 0; };
  uint32_t encode(Buffer::Instance&) const override { return 0; };
};

// Tests.

class KafkaBrokerFilterUnitTest : public testing::Test {
protected:
  MockKafkaMetricsFacadeSharedPtr metrics_{std::make_shared<MockKafkaMetricsFacade>()};
  MockResponseDecoderSharedPtr response_decoder_{std::make_shared<MockResponseDecoder>()};
  MockRequestDecoderSharedPtr request_decoder_{std::make_shared<MockRequestDecoder>()};

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  KafkaBrokerFilter testee_{metrics_, response_decoder_, request_decoder_};

  void initialize() {
    testee_.initializeReadFilterCallbacks(filter_callbacks_);
    testee_.onNewConnection();
  }
};

TEST_F(KafkaBrokerFilterUnitTest, shouldAcceptDataSentByKafkaClient) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_));

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::Continue);
  // Also, request_decoder got invoked.
}

TEST_F(KafkaBrokerFilterUnitTest, shouldStopIterationIfProcessingDataFromKafkaClientFails) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_)).WillOnce(Throw(EnvoyException("boom")));
  EXPECT_CALL(*request_decoder_, reset());
  EXPECT_CALL(*metrics_, onRequestException());

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(KafkaBrokerFilterUnitTest, shouldAcceptDataSentByKafkaBroker) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*response_decoder_, onData(_));

  // when
  initialize();
  const auto result = testee_.onWrite(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::Continue);
  // Also, request_decoder got invoked.
}

TEST_F(KafkaBrokerFilterUnitTest, shouldStopIterationIfProcessingDataFromKafkaBrokerFails) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*response_decoder_, onData(_)).WillOnce(Throw(EnvoyException("boom")));
  EXPECT_CALL(*response_decoder_, reset());
  EXPECT_CALL(*metrics_, onResponseException());

  // when
  initialize();
  const auto result = testee_.onWrite(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

class ForwarderUnitTest : public testing::Test {
protected:
  MockResponseDecoderSharedPtr response_decoder_{std::make_shared<MockResponseDecoder>()};
  Forwarder testee_{*response_decoder_};
};

TEST_F(ForwarderUnitTest, shouldUpdateResponseDecoderState) {
  // given
  const int16_t api_key = 42;
  const int16_t api_version = 13;
  const int32_t correlation_id = 1234;
  AbstractRequestSharedPtr request =
      std::make_shared<MockRequest>(api_key, api_version, correlation_id);

  EXPECT_CALL(*response_decoder_, expectResponse(correlation_id, api_key, api_version));

  // when
  testee_.onMessage(request);

  // then - response_decoder_ had a new expected response registered.
}

TEST_F(ForwarderUnitTest, shouldUpdateResponseDecoderStateOnFailedParse) {
  // given
  const int16_t api_key = 42;
  const int16_t api_version = 13;
  const int32_t correlation_id = 1234;
  RequestHeader header = {api_key, api_version, correlation_id, ""};
  RequestParseFailureSharedPtr parse_failure = std::make_shared<RequestParseFailure>(header);

  EXPECT_CALL(*response_decoder_, expectResponse(correlation_id, api_key, api_version));

  // when
  testee_.onFailedParse(parse_failure);

  // then - response_decoder_ had a new expected response registered.
}

class KafkaMetricsFacadeImplUnitTest : public testing::Test {
protected:
  MockTimeSource time_source_;
  std::shared_ptr<MockRichRequestMetrics> request_metrics_ =
      std::make_shared<MockRichRequestMetrics>();
  std::shared_ptr<MockRichResponseMetrics> response_metrics_ =
      std::make_shared<MockRichResponseMetrics>();
  KafkaMetricsFacadeImpl testee_{time_source_, request_metrics_, response_metrics_};
};

TEST_F(KafkaMetricsFacadeImplUnitTest, shouldRegisterRequest) {
  // given
  const int16_t api_key = 42;
  const int32_t correlation_id = 1234;
  AbstractRequestSharedPtr request = std::make_shared<MockRequest>(api_key, 0, correlation_id);

  EXPECT_CALL(*request_metrics_, onRequest(api_key));

  MonotonicTime time_point{MonotonicTime::duration(1234)};
  EXPECT_CALL(time_source_, monotonicTime()).WillOnce(Return(time_point));

  // when
  testee_.onMessage(request);

  // then
  const auto& request_arrivals = testee_.getRequestArrivalsForTest();
  ASSERT_EQ(request_arrivals.at(correlation_id), time_point);
}

TEST_F(KafkaMetricsFacadeImplUnitTest, shouldRegisterUnknownRequest) {
  // given
  RequestHeader header = {0, 0, 0, ""};
  RequestParseFailureSharedPtr unknown_request = std::make_shared<RequestParseFailure>(header);

  EXPECT_CALL(*request_metrics_, onUnknownRequest());

  // when
  testee_.onFailedParse(unknown_request);

  // then - request_metrics_ is updated.
}

TEST_F(KafkaMetricsFacadeImplUnitTest, shouldRegisterResponse) {
  // given
  const int16_t api_key = 42;
  const int32_t correlation_id = 1234;
  AbstractResponseSharedPtr response = std::make_shared<MockResponse>(api_key, correlation_id);

  MonotonicTime request_time_point{MonotonicTime::duration(1234000000)};
  testee_.getRequestArrivalsForTest()[correlation_id] = request_time_point;

  MonotonicTime response_time_point{MonotonicTime::duration(2345000000)};

  EXPECT_CALL(*response_metrics_, onResponse(api_key, 1111));
  EXPECT_CALL(time_source_, monotonicTime()).WillOnce(Return(response_time_point));

  // when
  testee_.onMessage(response);

  // then
  const auto& request_arrivals = testee_.getRequestArrivalsForTest();
  ASSERT_EQ(request_arrivals.find(correlation_id), request_arrivals.end());
}

TEST_F(KafkaMetricsFacadeImplUnitTest, shouldRegisterUnknownResponse) {
  // given
  ResponseMetadataSharedPtr unknown_response = std::make_shared<ResponseMetadata>(0, 0, 0);

  EXPECT_CALL(*response_metrics_, onUnknownResponse());

  // when
  testee_.onFailedParse(unknown_response);

  // then - response_metrics_ is updated.
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
