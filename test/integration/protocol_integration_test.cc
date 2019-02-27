#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "common/api/api_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/fmt.h"
#include "common/common/thread_annotations.h"
#include "common/http/headers.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/test_host_predicate_config.h"
#include "test/integration/utility.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::HasSubstr;
using testing::Invoke;
using testing::Not;

namespace Envoy {
namespace {
// Tests for DownstreamProtocolIntegrationTest will be run with all protocols
// (H1/H2 downstream) but only H1 upstreams.
//
// This is useful for things which will likely not differ based on upstream
// behavior, for example "how does Envoy handle duplicate content lengths from
// downstream"?
typedef HttpProtocolIntegrationTest DownstreamProtocolIntegrationTest;

// Tests for ProtocolIntegrationTest will be run with the full mesh of H1/H2
// downstream and H1/H2 upstreams.
typedef HttpProtocolIntegrationTest ProtocolIntegrationTest;

TEST_P(ProtocolIntegrationTest, ShutdownWithActiveConnPoolConnections) {
  auto response = makeHeaderOnlyRequest(nullptr, 0);
  // Shut down the server with active connection pool connections.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  test_server_.reset();
  checkSimpleRequestSuccess(0U, 0U, response.get());
}

// Change the default route to be restrictive, and send a request to an alternate route.
TEST_P(ProtocolIntegrationTest, RouterNotFound) { testRouterNotFound(); }

// Change the default route to be restrictive, and send a POST to an alternate route.
TEST_P(DownstreamProtocolIntegrationTest, RouterNotFoundBodyNoBuffer) {
  testRouterNotFoundWithBody();
}

// Add a route that uses unknown cluster (expect 404 Not Found).
TEST_P(DownstreamProtocolIntegrationTest, RouterClusterNotFound404) {
  config_helper_.addRoute("foo.com", "/unknown", "unknown_cluster", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
}

// Add a route that uses unknown cluster (expect 503 Service Unavailable).
TEST_P(DownstreamProtocolIntegrationTest, RouterClusterNotFound503) {
  config_helper_.addRoute("foo.com", "/unknown", "unknown_cluster", false,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::NONE);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/unknown", "", downstream_protocol_, version_, "foo.com");
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

// Add a route which redirects HTTP to HTTPS, and verify Envoy sends a 301
TEST_P(ProtocolIntegrationTest, RouterRedirect) {
  config_helper_.addRoute("www.redirect.com", "/", "cluster_0", true,
                          envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE,
                          envoy::api::v2::route::VirtualHost::ALL);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/foo", "", downstream_protocol_, version_, "www.redirect.com");
  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("301", response->headers().Status()->value().c_str());
  EXPECT_STREQ("https://www.redirect.com/foo",
               response->headers().get(Http::Headers::get().Location)->value().c_str());
}

// Add a health check filter and verify correct computation of health based on upstream status.
TEST_P(ProtocolIntegrationTest, ComputedHealthCheck) {
  config_helper_.addFilter(R"EOF(
name: envoy.health_check
config:
    pass_through_mode: false
    cluster_min_healthy_percentages:
        example_cluster_name: { value: 75 }
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

// Add a health check filter and verify correct computation of health based on upstream status.
TEST_P(ProtocolIntegrationTest, ModifyBuffer) {
  config_helper_.addFilter(R"EOF(
name: envoy.health_check
config:
    pass_through_mode: false
    cluster_min_healthy_percentages:
        example_cluster_name: { value: 75 }
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{
      {":method", "GET"}, {":path", "/healthcheck"}, {":scheme", "http"}, {":authority", "host"}});
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

TEST_P(ProtocolIntegrationTest, AddEncodedTrailers) {
  config_helper_.addFilter(R"EOF(
name: add-trailers-filter
config: {}
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  response->waitForEndStream();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    EXPECT_STREQ("decode", upstream_request_->trailers()->GrpcMessage()->value().c_str());
  }
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_STREQ("encode", response->trailers()->GrpcMessage()->value().c_str());
  }
}

// Add a health check filter and verify correct behavior when draining.
TEST_P(ProtocolIntegrationTest, DrainClose) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();

  test_server_->drainManager().draining_ = true;
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  response->waitForEndStream();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }

  test_server_->drainManager().draining_ = false;
}

TEST_P(ProtocolIntegrationTest, Retry) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

// Tests that the x-envoy-attempt-count header is properly set on the upstream request
// and updated after the request is retried.
TEST_P(DownstreamProtocolIntegrationTest, RetryAttemptCountHeader) {
  config_helper_.addRoute("host", "/test_retry", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, {}, true);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test_retry"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  EXPECT_EQ(atoi(upstream_request_->headers().EnvoyAttemptCount()->value().c_str()), 1);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
  waitForNextUpstreamRequest();
  EXPECT_EQ(atoi(upstream_request_->headers().EnvoyAttemptCount()->value().c_str()), 2);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

// Verifies that a retry priority can be configured and affect the host selected during retries.
// The retry priority will always target P1, which would otherwise never be hit due to P0 being
// healthy.
TEST_P(DownstreamProtocolIntegrationTest, RetryPriority) {
  const Upstream::HealthyLoad healthy_priority_load({0u, 100u});
  const Upstream::DegradedLoad degraded_priority_load({0u, 100u});
  NiceMock<Upstream::MockRetryPriority> retry_priority(healthy_priority_load,
                                                       degraded_priority_load);
  Upstream::MockRetryPriorityFactory factory(retry_priority);

  Registry::InjectFactory<Upstream::RetryPriorityFactory> inject_factory(factory);

  envoy::api::v2::route::RetryPolicy retry_policy;
  retry_policy.mutable_retry_priority()->set_name(factory.name());

  // Add route with custom retry policy
  config_helper_.addRoute("host", "/test_retry", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, retry_policy);

  // Use load assignments instead of static hosts. Necessary in order to use priorities.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    auto load_assignment = cluster->mutable_load_assignment();
    load_assignment->set_cluster_name(cluster->name());
    const auto& host_address = cluster->hosts(0).socket_address().address();

    for (int i = 0; i < 2; ++i) {
      auto locality = load_assignment->add_endpoints();
      locality->set_priority(i);
      locality->mutable_locality()->set_region("region");
      locality->mutable_locality()->set_zone("zone");
      locality->mutable_locality()->set_sub_zone("sub_zone" + std::to_string(i));
      auto lb_endpoint = locality->add_lb_endpoints();
      lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
          host_address);
      lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
          0);
    }

    cluster->clear_hosts();
  });

  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test_retry"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);

  // Note how we're expecting each upstream request to hit the same upstream.
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[0]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest(1);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

//
// Verifies that a retry host filter can be configured and affect the host selected during retries.
// The predicate will keep track of the first host attempted, and attempt to route all requests to
// the same host. With a total of two upstream hosts, this should result in us continuously sending
// requests to the same host.
TEST_P(DownstreamProtocolIntegrationTest, RetryHostPredicateFilter) {
  TestHostPredicateFactory predicate_factory;
  Registry::InjectFactory<Upstream::RetryHostPredicateFactory> inject_factory(predicate_factory);

  envoy::api::v2::route::RetryPolicy retry_policy;
  retry_policy.add_retry_host_predicate()->set_name(predicate_factory.name());

  // Add route with custom retry policy
  config_helper_.addRoute("host", "/test_retry", "cluster_0", false,
                          envoy::api::v2::route::RouteAction::NOT_FOUND,
                          envoy::api::v2::route::VirtualHost::NONE, retry_policy);

  // We want to work with a cluster with two hosts.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* new_host = bootstrap.mutable_static_resources()->mutable_clusters(0)->add_hosts();
    new_host->MergeFrom(bootstrap.static_resources().clusters(0).hosts(0));
  });
  fake_upstreams_count_ = 2;
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test_retry"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024);

  // Note how we're expecting each upstream request to hit the same upstream.
  auto upstream_idx = waitForNextUpstreamRequest({0, 1});
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  if (fake_upstreams_[upstream_idx]->httpType() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    ASSERT_TRUE(fake_upstreams_[upstream_idx]->waitForHttpConnection(*dispatcher_,
                                                                     fake_upstream_connection_));
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  waitForNextUpstreamRequest(upstream_idx);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(512, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(512U, response->body().size());
}

// Very similar set-up to testRetry but with a 16k request the request will not
// be buffered and the 503 will be returned to the user.
TEST_P(ProtocolIntegrationTest, RetryHittingBufferLimit) {
  config_helper_.setBufferLimits(1024, 1024); // Set buffer limits upstream and downstream.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024 * 65);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);

  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(66560U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
}

// Test hitting the dynamo filter with too many request bytes to buffer. Ensure the connection
// manager sends a 413.
TEST_P(DownstreamProtocolIntegrationTest, HittingDecoderFilterLimit) {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Envoy will likely connect and proxy some unspecified amount of data before
  // hitting the buffer limit and disconnecting. Ignore this if it happens.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"x-forwarded-for", "10.0.0.1"},
                                                                 {"x-envoy-retry-on", "5xx"}},
                                         1024 * 65);

  response->waitForEndStream();
  // With HTTP/1 there's a possible race where if the connection backs up early,
  // the 413-and-connection-close may be sent while the body is still being
  // sent, resulting in a write error and the connection being closed before the
  // response is read.
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    ASSERT_TRUE(response->complete());
  }
  if (response->complete()) {
    EXPECT_STREQ("413", response->headers().Status()->value().c_str());
  }
}

// Test hitting the dynamo filter with too many response bytes to buffer. Given the request headers
// are sent on early, the stream/connection will be reset.
TEST_P(DownstreamProtocolIntegrationTest, HittingEncoderFilterLimit) {
  config_helper_.addFilter("{ name: envoy.http_dynamo_filter, config: {} }");
  config_helper_.setBufferLimits(1024, 1024);
  initialize();

  // Send the request.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto downstream_request = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  Buffer::OwnedImpl data("{\"TableName\":\"locations\"}");
  codec_client_->sendData(*downstream_request, data, true);
  waitForNextUpstreamRequest();

  // Send the response headers.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Now send an overly large response body. At some point, too much data will
  // be buffered, the stream will be reset, and the connection will disconnect.
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("500", response->headers().Status()->value().c_str());
}

TEST_P(ProtocolIntegrationTest, EnvoyHandling100Continue) { testEnvoyHandling100Continue(); }

TEST_P(ProtocolIntegrationTest, EnvoyHandlingDuplicate100Continue) {
  testEnvoyHandling100Continue(true);
}

TEST_P(ProtocolIntegrationTest, EnvoyProxyingEarly100Continue) {
  testEnvoyProxying100Continue(true);
}

TEST_P(ProtocolIntegrationTest, EnvoyProxyingLate100Continue) {
  testEnvoyProxying100Continue(false);
}

TEST_P(ProtocolIntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(ProtocolIntegrationTest, TwoRequestsWithForcedBackup) { testTwoRequests(true); }

TEST_P(DownstreamProtocolIntegrationTest, ValidZeroLengthContent) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"content-length", "0"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(DownstreamProtocolIntegrationTest, InvalidContentLength) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":authority", "host"},
                                                          {"content-length", "-1"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, MultipleContentLengths) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "POST"},
                                                          {":path", "/test/long/url"},
                                                          {":authority", "host"},
                                                          {"content-length", "3,2"}});
  auto response = std::move(encoder_decoder.second);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    codec_client_->waitForDisconnect();
  } else {
    response->waitForReset();
    codec_client_->close();
  }

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(response->complete());
    EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::RemoteReset, response->reset_reason());
  }
}

TEST_P(DownstreamProtocolIntegrationTest, HeadersOnlyFilterEncoding) {
  config_helper_.addFilter(R"EOF(
name: encode-headers-only
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  response->waitForEndStream();
  EXPECT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ(0, response->body().size());
}

TEST_P(DownstreamProtocolIntegrationTest, HeadersOnlyFilterDecoding) {
  config_helper_.addFilter(R"EOF(
name: decode-headers-only
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ(128, response->body().size());
}

TEST_P(DownstreamProtocolIntegrationTest, HeadersOnlyFilterEncodingIntermediateFilters) {
  config_helper_.addFilter(R"EOF(
name: passthrough-filter
)EOF");
  config_helper_.addFilter(R"EOF(
name: encode-headers-only
)EOF");
  config_helper_.addFilter(R"EOF(
name: passthrough-filter
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  response->waitForEndStream();
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ(0, response->body().size());
}

TEST_P(DownstreamProtocolIntegrationTest, HeadersOnlyFilterDecodingIntermediateFilters) {
  config_helper_.addFilter(R"EOF(
name: passthrough-filter
)EOF");
  config_helper_.addFilter(R"EOF(
name: decode-headers-only
)EOF");
  config_helper_.addFilter(R"EOF(
name: passthrough-filter
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      codec_client_->makeRequestWithBody(Http::TestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}},
                                         128);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);
  upstream_request_->encodeData(128, true);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ(128, response->body().size());
}

// Verifies behavior when request data is encoded after the request has been
// turned into a headers-only request and the response has already begun.
TEST_P(DownstreamProtocolIntegrationTest, HeadersOnlyFilterInterleaved) {
  config_helper_.addFilter(R"EOF(
name: decode-headers-only
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // First send the request headers. The filter should turn this into a header-only
  // request.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for the upstream request and begin sending a response with end_stream = false.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, false);

  // Simulate additional data after the request has been turned into a headers only request.
  Buffer::OwnedImpl data(std::string(128, 'a'));
  request_encoder_->encodeData(data, false);

  // End the response.
  upstream_request_->encodeData(128, true);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());
  EXPECT_EQ(0, upstream_request_->body().length());
}

// For tests which focus on downstream-to-Envoy behavior, and don't need to be
// run with both HTTP/1 and HTTP/2 upstreams.
INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

INSTANTIATE_TEST_SUITE_P(Protocols, ProtocolIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace
} // namespace Envoy
