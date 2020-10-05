#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {
namespace {

class FaultIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
public:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const std::string upstream_rate_limit_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.fault.v2.HTTPFault
  response_rate_limit:
    fixed_limit:
      limit_kbps: 1
    percentage:
      numerator: 100
)EOF";

  const std::string header_fault_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.fault.v2.HTTPFault
  abort:
    header_abort: {}
    percentage:
      numerator: 100
  delay:
    header_delay: {}
    percentage:
      numerator: 100
  response_rate_limit:
    header_limit: {}
    percentage:
      numerator: 100
)EOF";

  const std::string abort_grpc_fault_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    grpc_status: 5
    percentage:
      numerator: 100
)EOF";

  const std::string abort_all_with_filter_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.config.common.matcher.v3.MatchingFilterConfig
  match_tree:
    matcher:
      multimap_matcher:
        key_namespace: request_headers
        key: x-disable-faults
        exact_matches:
          disable:
            leaf:
              matchers:
                predicate:
                  any_match: {}
                action:
                  skip: true
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
    abort:
      http_status: 503
      percentage:
        numerator: 100
)EOF";
};

// Fault integration tests that should run with all protocols, useful for testing various
// end_stream permutations when rate limiting.
class FaultIntegrationTestAllProtocols : public FaultIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, FaultIntegrationTestAllProtocols,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// No fault injected.
TEST_P(FaultIntegrationTestAllProtocols, NoFault) {
  const std::string filter_config =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.fault.v2.HTTPFault
)EOF";

  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Response rate limited with no trailers.
TEST_P(FaultIntegrationTestAllProtocols, ResponseRateLimitNoTrailers) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Active faults gauge is incremented.
  EXPECT_EQ(1UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(127, 'a'));
  upstream_request_->encodeData(data, true);

  // Wait for a tick worth of data.
  decoder->waitForBodyData(64);

  // Wait for a tick worth of data and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(63));
  decoder->waitForBodyData(127);
  decoder->waitForEndStream();

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request delay and response rate limited via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-fault-delay-request", "200"},
                                                 {"x-envoy-fault-throughput-response", "1"}};
  IntegrationStreamDecoderPtr decoder = codec_client_->makeHeaderOnlyRequest(request_headers);
  test_server_->waitForCounterEq("http.config_test.fault.delays_injected", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());
  simTime().advanceTimeWait(std::chrono::milliseconds(200));
  waitForNextUpstreamRequest();

  // Verify response body throttling.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(128, 'a'));
  upstream_request_->encodeData(data, true);

  // Wait for a tick worth of data.
  decoder->waitForBodyData(64);

  // Wait for a tick worth of data and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(63));
  decoder->waitForBodyData(128);
  decoder->waitForEndStream();

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("429"));

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Faults are skipped when header matching matches specific header.
TEST_P(FaultIntegrationTestAllProtocols, MatchTree) {
  initializeFilter(abort_all_with_filter_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  {

    // First request should have a fault injected.
    auto response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/test/long/url"},
                                       {":scheme", "http"},
                                       {":authority", "host"}});
    response->waitForEndStream();

    EXPECT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("503"));
  }

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  // The second should not inject a fault since the match tree should cause the fault filter to be
  // skipped.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {"x-disable-faults", "disable"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request faults controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultsConfig0PercentageHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"},
                                     {"x-envoy-fault-abort-request-percentage", "0"},
                                     {"x-envoy-fault-delay-request", "100"},
                                     {"x-envoy-fault-delay-request-percentage", "0"},
                                     {"x-envoy-fault-throughput-response", "100"},
                                     {"x-envoy-fault-throughput-response-percentage", "0"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request faults controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultsConfig100PercentageHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-delay-request", "100"},
                                     {"x-envoy-fault-delay-request-percentage", "100"},
                                     {"x-envoy-fault-throughput-response", "100"},
                                     {"x-envoy-fault-throughput-response-percentage", "100"}});
  test_server_->waitForCounterEq("http.config_test.fault.delays_injected", 1,
                                 TestUtility::DefaultTimeout, dispatcher_.get());
  simTime().advanceTimeWait(std::chrono::milliseconds(100));
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Header configuration with no headers, so no fault injection.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultConfigNoHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortGrpcConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-grpc-request", "5"},
                                     {"content-type", "application/grpc"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "5"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().GrpcMessage, "fault filter abort"));
  EXPECT_EQ(nullptr, response->trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortGrpcConfig0PercentageHeader) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-grpc-request", "5"},
                                     {"x-envoy-fault-abort-request-percentage", "0"},
                                     {"content-type", "application/grpc"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via configuration.
TEST_P(FaultIntegrationTestAllProtocols, FaultAbortGrpcConfig) {
  initializeFilter(abort_grpc_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "5"));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().GrpcMessage, "fault filter abort"));
  EXPECT_EQ(nullptr, response->trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Fault integration tests that run with HTTP/2 only, used for fully testing trailers.
class FaultIntegrationTestHttp2 : public FaultIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, FaultIntegrationTestHttp2,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Rate limiting with trailers received after the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Active fault gauge is incremented.
  EXPECT_EQ(1UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(127, 'a'));
  upstream_request_->encodeData(data, false);

  // Wait for a tick worth of data.
  decoder->waitForBodyData(64);

  // Advance time and wait for a tick worth of data.
  simTime().advanceTimeWait(std::chrono::milliseconds(63));
  decoder->waitForBodyData(127);

  // Send trailers and wait for end stream.
  Http::TestResponseTrailerMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);
  decoder->waitForEndStream();
  EXPECT_NE(nullptr, decoder->trailers());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Rate limiting with trailers received before the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyNotFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(128, 'a'));
  upstream_request_->encodeData(data, false);
  Http::TestResponseTrailerMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);

  // Wait for a tick worth of data.
  decoder->waitForBodyData(64);

  // Advance time and wait for a tick worth of data, trailers, and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(63));
  decoder->waitForBodyData(128);
  decoder->waitForEndStream();
  EXPECT_NE(nullptr, decoder->trailers());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
