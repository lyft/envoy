#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {
const std::string CSRF_ENABLED_CONFIG = R"EOF(
name: envoy.csrf
config:
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_FILTER_ENABLED_CONFIG = R"EOF(
name: envoy.csrf
config:
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_SHADOW_ENABLED_CONFIG = R"EOF(
name: envoy.csrf
config:
  filter_enabled:
    default_value:
      numerator: 0
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
)EOF";

const std::string CSRF_DISABLED_CONFIG = R"EOF(
name: envoy.csrf
config:
  filter_enabled:
    default_value:
      numerator: 0
      denominator: HUNDRED
)EOF";

class CsrfFilterIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  const absl::string_view sendRequestAndWaitForResponse(Http::TestHeaderMapImpl&& request_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
    response->waitForEndStream();

    return response->complete() ? response->headers().Status()->value().getStringView()
                                : absl::string_view("incomplete");
  }

  const absl::string_view sendRequest(Http::TestHeaderMapImpl&& request_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    response->waitForEndStream();

    return response->complete() ? response->headers().Status()->value().getStringView()
                                : absl::string_view("incomplete");
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, CsrfFilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(CsrfFilterIntegrationTest, TestCsrfSuccess) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequestAndWaitForResponse(Http::TestHeaderMapImpl{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "localhost"},
      {"host", "localhost"},
  });
  EXPECT_EQ("200", result);
}

TEST_P(CsrfFilterIntegrationTest, TestCsrfDisabled) {
  config_helper_.addFilter(CSRF_DISABLED_CONFIG);
  const auto& result = sendRequestAndWaitForResponse(Http::TestHeaderMapImpl{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  });
  EXPECT_EQ("200", result);
}

TEST_P(CsrfFilterIntegrationTest, TestNonMutationMethod) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequestAndWaitForResponse(Http::TestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  });
  EXPECT_EQ("200", result);
}

TEST_P(CsrfFilterIntegrationTest, TestOriginMismatch) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequest(Http::TestHeaderMapImpl{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  });
  EXPECT_EQ("403", result);
}

TEST_P(CsrfFilterIntegrationTest, TestEnforcesPost) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequest(Http::TestHeaderMapImpl{
      {":method", "POST"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  });
  EXPECT_EQ("403", result);
}

TEST_P(CsrfFilterIntegrationTest, TestEnforcesDelete) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequest(Http::TestHeaderMapImpl{
      {":method", "DELETE"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "test-origin"},
  });
  EXPECT_EQ("403", result);
}

TEST_P(CsrfFilterIntegrationTest, TestRefererFallback) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result =
      sendRequestAndWaitForResponse(Http::TestHeaderMapImpl{{":method", "DELETE"},
                                                            {":path", "/"},
                                                            {":scheme", "http"},
                                                            {"referer", "test-origin"},
                                                            {"host", "test-origin"}});
  EXPECT_EQ("200", result);
}

TEST_P(CsrfFilterIntegrationTest, TestMissingOrigin) {
  config_helper_.addFilter(CSRF_FILTER_ENABLED_CONFIG);
  const auto& result = sendRequest(Http::TestHeaderMapImpl{
      {":method", "DELETE"}, {":path", "/"}, {":scheme", "http"}, {"host", "test-origin"}});
  EXPECT_EQ("403", result);
}
TEST_P(CsrfFilterIntegrationTest, TestShadowOnlyMode) {
  config_helper_.addFilter(CSRF_SHADOW_ENABLED_CONFIG);
  const auto& result = sendRequestAndWaitForResponse(Http::TestHeaderMapImpl{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "localhost"},
  });
  EXPECT_EQ("200", result);
}
TEST_P(CsrfFilterIntegrationTest, TestFilterAndShadowEnabled) {
  config_helper_.addFilter(CSRF_ENABLED_CONFIG);
  const auto& result = sendRequest(Http::TestHeaderMapImpl{
      {":method", "PUT"},
      {":path", "/"},
      {":scheme", "http"},
      {"origin", "cross-origin"},
      {"host", "localhost"},
  });
  EXPECT_EQ("403", result);
}
} // namespace
} // namespace Envoy
