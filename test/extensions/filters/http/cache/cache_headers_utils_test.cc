#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/common/macros.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/http/cache/cache_headers_utils.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

struct TestRequestCacheControl : public RequestCacheControl {
  TestRequestCacheControl(bool must_validate, bool no_store, bool no_transform, bool only_if_cached,
                          OptionalDuration max_age, OptionalDuration min_fresh,
                          OptionalDuration max_stale) {
    must_validate_ = must_validate;
    no_store_ = no_store;
    no_transform_ = no_transform;
    only_if_cached_ = only_if_cached;
    max_age_ = max_age;
    min_fresh_ = min_fresh;
    max_stale_ = max_stale;
  }
};

struct TestResponseCacheControl : public ResponseCacheControl {
  TestResponseCacheControl(bool must_validate, bool no_store, bool no_transform, bool no_stale,
                           bool is_public, OptionalDuration max_age) {
    must_validate_ = must_validate;
    no_store_ = no_store;
    no_transform_ = no_transform;
    no_stale_ = no_stale;
    is_public_ = is_public;
    max_age_ = max_age;
  }
};

struct RequestCacheControlTestCase {
  absl::string_view cache_control_header;
  TestRequestCacheControl request_cache_control;
};

struct ResponseCacheControlTestCase {
  absl::string_view cache_control_header;
  TestResponseCacheControl response_cache_control;
};

class RequestCacheControlTest : public testing::TestWithParam<RequestCacheControlTestCase> {
public:
  static const std::vector<RequestCacheControlTestCase>& getTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<RequestCacheControlTestCase>,
        // Empty header
        {
          "",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        // Valid cache-control headers
        {
          "max-age=3600, min-fresh=10, no-transform, only-if-cached, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, true, true, std::chrono::seconds(3600), std::chrono::seconds(10), absl::nullopt}
        },
        {
          "min-fresh=100, max-stale, no-cache",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, false, false, false, absl::nullopt, std::chrono::seconds(100), SystemTime::duration::max()}
        },
        {
          "max-age=10, max-stale=50",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Quoted arguments are interpreted correctly
        {
          "max-age=\"3600\", min-fresh=\"10\", no-transform, only-if-cached, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, true, true, std::chrono::seconds(3600), std::chrono::seconds(10), absl::nullopt}
        },
        {
          "max-age=\"10\", max-stale=\"50\", only-if-cached",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, true, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Unknown directives are ignored
        {
          "max-age=10, max-stale=50, unknown-directive",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-arg=arg1",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive-with-quoted-arg=\"arg1\"",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        {
          "max-age=10, max-stale=50, unknown-directive, unknown-directive-with-quoted-arg=\"arg1\"",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, std::chrono::seconds(10), absl::nullopt, std::chrono::seconds(50)}
        },
        // Invalid durations are ignored
        {
          "max-age=five, min-fresh=30, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, true, false, false, absl::nullopt, std::chrono::seconds(30), absl::nullopt}
        },
        {
          "max-age=five, min-fresh=30s, max-stale=-2",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        {
          "max-age=\"", 
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {false, false, false, false, absl::nullopt, absl::nullopt, absl::nullopt}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20, min-fresh=30=40",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, false, false, false, std::chrono::seconds(20), absl::nullopt, absl::nullopt}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store",
          // {must_validate_, no_store_, no_transform_, only_if_cached_, max_age_, min_fresh_, max_stale_}
          {true, true, false, false, std::chrono::seconds(10), absl::nullopt, absl::nullopt}
        },
    );
    // clang-format on
  }
};

class ResponseCacheControlTest : public testing::TestWithParam<ResponseCacheControlTestCase> {
public:
  static const std::vector<ResponseCacheControlTestCase>& getTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<ResponseCacheControlTestCase>,
        // Empty header
        {
          "", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        // Valid cache-control headers
        {
          "s-maxage=1000, max-age=2000, proxy-revalidate, no-store",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, true, false, std::chrono::seconds(1000)}
        },
        {
          "max-age=500, must-revalidate, no-cache, no-transform",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, true, true, false, std::chrono::seconds(500)}
        },
        {
          "s-maxage=10, private=content-length, no-cache=content-encoding",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(10)}
        },
        {
          "private",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, absl::nullopt}
        },
        {
          "public, max-age=0",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, true, std::chrono::seconds(0)}
        },
        // Quoted arguments are interpreted correctly
        {
          "s-maxage=\"20\", max-age=\"10\", public",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, true, std::chrono::seconds(20)}
        },
        {
          "max-age=\"50\", private",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, std::chrono::seconds(50)}
        },
        {
          "s-maxage=\"0\"", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, std::chrono::seconds(0)}
        },
        // Unknown directives are ignored
        {
          "private, no-cache, max-age=30, unknown-directive",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-arg=arg",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive-with-quoted-arg=\"arg\"",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        {
          "private, no-cache, max-age=30, unknown-directive, unknown-directive-with-quoted-arg=\"arg\"",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(30)}
        },
        // Invalid durations are ignored
        {
          "max-age=five", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        {
          "max-age=10s, private", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, true, false, false, false, absl::nullopt}
        },
        {
          "s-maxage=\"50s\", max-age=\"zero\", no-cache",
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, false, false, false, absl::nullopt}
        },
        {
          "s-maxage=five, max-age=10, no-transform", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, true, false, false, std::chrono::seconds(10)}
        },
        {
          "max-age=\"", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {false, false, false, false, false, absl::nullopt}
        },
        // Invalid parts of the header are ignored
        {
          "no-cache, ,,,fjfwioen3298, max-age=20", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, false, false, false, false, std::chrono::seconds(20)}
        },
        // If a directive argument contains a comma by mistake
        // the part before the comma will be interpreted as the argument
        // and the part after it will be ignored
        {
          "no-cache, max-age=10,0, no-store", 
          // {must_validate_, no_store_, no_transform_, no_stale_, is_public_, max_age_}
          {true, true, false, false, false, std::chrono::seconds(10)}
        },
    );
    // clang-format on
  }
};

// TODO(#9872): More tests for httpTime.
class HttpTimeTest : public testing::TestWithParam<std::string> {
public:
  static const std::vector<std::string>& getOkTestCases() {
    // clang-format off
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>,
        "Sun, 06 Nov 1994 08:49:37 GMT",  // IMF-fixdate.
        "Sunday, 06-Nov-94 08:49:37 GMT", // obsolete RFC 850 format.
        "Sun Nov  6 08:49:37 1994"        // ANSI C's asctime() format.
    );
    // clang-format on
  }
};

INSTANTIATE_TEST_SUITE_P(RequestCacheControlTest, RequestCacheControlTest,
                         testing::ValuesIn(RequestCacheControlTest::getTestCases()));

TEST_P(RequestCacheControlTest, RequestCacheControlTest) {
  const absl::string_view cache_control_header = GetParam().cache_control_header;
  const RequestCacheControl expected_request_cache_control = GetParam().request_cache_control;
  EXPECT_EQ(expected_request_cache_control, RequestCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(ResponseCacheControlTest, ResponseCacheControlTest,
                         testing::ValuesIn(ResponseCacheControlTest::getTestCases()));

TEST_P(ResponseCacheControlTest, ResponseCacheControlTest) {
  const absl::string_view cache_control_header = GetParam().cache_control_header;
  const ResponseCacheControl expected_response_cache_control = GetParam().response_cache_control;
  EXPECT_EQ(expected_response_cache_control, ResponseCacheControl(cache_control_header));
}

INSTANTIATE_TEST_SUITE_P(Ok, HttpTimeTest, testing::ValuesIn(HttpTimeTest::getOkTestCases()));

TEST_P(HttpTimeTest, OkFormats) {
  const Http::TestResponseHeaderMapImpl response_headers{{"date", GetParam()}};
  // Manually confirmed that 784111777 is 11/6/94, 8:46:37.
  EXPECT_EQ(784111777,
            SystemTime::clock::to_time_t(CacheHeadersUtils::httpTime(response_headers.Date())));
}

TEST(HttpTime, InvalidFormat) {
  const std::string invalid_format_date = "Sunday, 06-11-1994 08:49:37";
  const Http::TestResponseHeaderMapImpl response_headers{{"date", invalid_format_date}};
  EXPECT_EQ(CacheHeadersUtils::httpTime(response_headers.Date()), SystemTime());
}

TEST(HttpTime, Null) { EXPECT_EQ(CacheHeadersUtils::httpTime(nullptr), SystemTime()); }

void testReadAndRemoveLeadingDigits(absl::string_view input, int64_t expected,
                                    absl::string_view remaining) {
  absl::string_view test_input(input);
  auto output = CacheHeadersUtils::readAndRemoveLeadingDigits(test_input);
  if (output) {
    EXPECT_EQ(output, static_cast<uint64_t>(expected)) << "input=" << input;
    EXPECT_EQ(test_input, remaining) << "input=" << input;
  } else {
    EXPECT_LT(expected, 0) << "input=" << input;
    EXPECT_EQ(test_input, remaining) << "input=" << input;
  }
}

TEST(ReadAndRemoveLeadingDigits, ComprehensiveTest) {
  testReadAndRemoveLeadingDigits("123", 123, "");
  testReadAndRemoveLeadingDigits("a123", -1, "a123");
  testReadAndRemoveLeadingDigits("9_", 9, "_");
  testReadAndRemoveLeadingDigits("11111111111xyz", 11111111111ll, "xyz");

  // Overflow case
  testReadAndRemoveLeadingDigits("1111111111111111111111111111111xyz", -1,
                                 "1111111111111111111111111111111xyz");

  // 2^64
  testReadAndRemoveLeadingDigits("18446744073709551616xyz", -1, "18446744073709551616xyz");
  // 2^64-1
  testReadAndRemoveLeadingDigits("18446744073709551615xyz", 18446744073709551615ull, "xyz");
  // (2^64-1)*10+9
  testReadAndRemoveLeadingDigits("184467440737095516159yz", -1, "184467440737095516159yz");
}

TEST(GetAllMatchingHeaderNames, EmptyRuleset) {
  Http::TestRequestHeaderMapImpl headers{{"accept", "image/*"}};
  std::vector<Matchers::StringMatcherPtr> ruleset;
  absl::flat_hash_set<absl::string_view> result;

  CacheHeadersUtils::getAllMatchingHeaderNames(headers, ruleset, result);

  ASSERT_TRUE(result.empty());
}

TEST(GetAllMatchingHeaderNames, EmptyHeaderMap) {
  Http::TestRequestHeaderMapImpl headers;
  std::vector<Matchers::StringMatcherPtr> ruleset;
  absl::flat_hash_set<absl::string_view> result;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("accept");
  ruleset.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

  CacheHeadersUtils::getAllMatchingHeaderNames(headers, ruleset, result);

  ASSERT_TRUE(result.empty());
}

TEST(GetAllMatchingHeaderNames, SingleMatchSingleValue) {
  Http::TestRequestHeaderMapImpl headers{{"accept", "image/*"}, {"accept-language", "en-US"}};
  std::vector<Matchers::StringMatcherPtr> ruleset;
  absl::flat_hash_set<absl::string_view> result;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("accept");
  ruleset.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

  CacheHeadersUtils::getAllMatchingHeaderNames(headers, ruleset, result);

  ASSERT_EQ(result.size(), 1);
  EXPECT_TRUE(result.contains("accept"));
}

TEST(GetAllMatchingHeaderNames, SingleMatchMultiValue) {
  Http::TestRequestHeaderMapImpl headers{{"accept", "image/*"}, {"accept", "text/html"}};
  std::vector<Matchers::StringMatcherPtr> ruleset;
  absl::flat_hash_set<absl::string_view> result;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("accept");
  ruleset.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

  CacheHeadersUtils::getAllMatchingHeaderNames(headers, ruleset, result);

  ASSERT_EQ(result.size(), 1);
  EXPECT_TRUE(result.contains("accept"));
}

TEST(GetAllMatchingHeaderNames, MultipleMatches) {
  Http::TestRequestHeaderMapImpl headers{{"accept", "image/*"}, {"accept-language", "en-US"}};
  std::vector<Matchers::StringMatcherPtr> ruleset;
  absl::flat_hash_set<absl::string_view> result;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact("accept");
  ruleset.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));
  matcher.set_exact("accept-language");
  ruleset.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

  CacheHeadersUtils::getAllMatchingHeaderNames(headers, ruleset, result);

  ASSERT_EQ(result.size(), 2);
  EXPECT_TRUE(result.contains("accept"));
  EXPECT_TRUE(result.contains("accept-language"));
}

TEST(HasVary, Null) {
  Http::TestResponseHeaderMapImpl headers;
  ASSERT_FALSE(VaryHeader::hasVary(headers));
}

TEST(HasVary, Empty) {
  Http::TestResponseHeaderMapImpl headers{{"vary", ""}};
  ASSERT_FALSE(VaryHeader::hasVary(headers));
}

TEST(HasVary, NotEmpty) {
  Http::TestResponseHeaderMapImpl headers{{"vary", "accept"}};
  ASSERT_TRUE(VaryHeader::hasVary(headers));
}

TEST(ParseHeaderValue, Null) {
  Http::TestResponseHeaderMapImpl headers;
  std::vector<std::string> result =
      VaryHeader::parseHeaderValue(headers.get(Http::Headers::get().Vary));

  EXPECT_EQ(result.size(), 0);
}

TEST(ParseHeaderValue, Empty) {
  Http::TestResponseHeaderMapImpl headers{{"vary", ""}};
  std::vector<std::string> result =
      VaryHeader::parseHeaderValue(headers.get(Http::Headers::get().Vary));

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "");
}

TEST(ParseHeaderValue, SingleValue) {
  Http::TestResponseHeaderMapImpl headers{{"vary", "accept"}};
  std::vector<std::string> result =
      VaryHeader::parseHeaderValue(headers.get(Http::Headers::get().Vary));

  EXPECT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], "accept");
}

class ParseHeaderValueMultipleTest : public testing::Test,
                                     public testing::WithParamInterface<std::string> {
protected:
  Http::TestResponseHeaderMapImpl headers{{"vary", GetParam()}};
};

INSTANTIATE_TEST_SUITE_P(MultipleValuesMixedSpaces, ParseHeaderValueMultipleTest,
                         testing::Values("accept,accept-language", " accept,accept-language",
                                         "accept ,accept-language", "accept, accept-language",
                                         "accept,accept-language ", " accept, accept-language ",
                                         "  accept  ,  accept-language  "));

TEST_P(ParseHeaderValueMultipleTest, MultipleValuesMixedSpaces) {
  std::vector<std::string> result =
      VaryHeader::parseHeaderValue(headers.get(Http::Headers::get().Vary));
  EXPECT_EQ(result.size(), 2);
  EXPECT_EQ(result[0], "accept");
  EXPECT_EQ(result[1], "accept-language");
}

TEST(CreateVaryKey, EmptyVaryEntry) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", ""}};
  Http::TestRequestHeaderMapImpl request_headers{{"accept", "image/*"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\n\r\n");
}

TEST(CreateVaryKey, SingleHeaderExists) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept"}};
  Http::TestRequestHeaderMapImpl request_headers{{"accept", "image/*"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r"
      "image/*\n");
}

TEST(CreateVaryKey, SingleHeaderMissing) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept"}};
  Http::TestRequestHeaderMapImpl request_headers;

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r\n");
}

TEST(CreateVaryKey, MultipleHeadersAllExist) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept, accept-language, width"}};
  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "image/*"}, {"accept-language", "en-us"}, {"width", "640"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r"
      "image/*\naccept-language\r"
      "en-us\nwidth\r640\n");
}

TEST(CreateVaryKey, MultipleHeadersSomeExist) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept, accept-language, width"}};
  Http::TestRequestHeaderMapImpl request_headers{{"accept", "image/*"}, {"width", "640"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r"
      "image/*\naccept-language\r\nwidth\r640\n");
}

TEST(CreateVaryKey, ExtraRequestHeaders) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept, width"}};
  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "image/*"}, {"heigth", "1280"}, {"width", "640"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r"
      "image/*\nwidth\r640\n");
}

TEST(CreateVaryKey, MultipleHeadersNoneExist) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept, accept-language, width"}};
  Http::TestRequestHeaderMapImpl request_headers;

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\naccept\r\naccept-language\r\nwidth\r\n");
}

TEST(CreateVaryKey, DifferentHeadersSameValue) {
  // Two requests with the same value for different headers must have different vary-keys.
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "accept, accept-language"}};

  Http::TestRequestHeaderMapImpl request_headers1{{"accept", "foo"}};
  std::string vary_key1 =
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers1);

  Http::TestRequestHeaderMapImpl request_headers2{{"accept-language", "foo"}};
  std::string vary_key2 =
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers2);

  ASSERT_NE(vary_key1, vary_key2);
}

TEST(CreateVaryKey, MultiValueSameHeader) {
  Http::TestResponseHeaderMapImpl response_headers{{"vary", "width"}};
  Http::TestRequestHeaderMapImpl request_headers{{"width", "foo"}, {"width", "bar"}};

  ASSERT_EQ(
      VaryHeader::createVaryKey(response_headers.get(Http::Headers::get().Vary), request_headers),
      "vary-key\nwidth\r"
      "foo\r"
      "bar\n");
}

TEST(VaryParseAllowlist, Empty) {
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  std::vector<Matchers::StringMatcherPtr> allowlist =
      VaryHeader::parseAllowlist(config.allowed_vary_headers());
  ASSERT_TRUE(allowlist.empty());
}

TEST(VaryParseAllowlist, SingleRule) {
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;
  const auto& add_new_rule = config.mutable_allowed_vary_headers()->Add();
  add_new_rule->set_exact("accept");

  std::vector<Matchers::StringMatcherPtr> allowlist =
      VaryHeader::parseAllowlist(config.allowed_vary_headers());
  ASSERT_EQ(allowlist.size(), 1);
  EXPECT_TRUE(allowlist[0]->match("accept"));
  EXPECT_FALSE(allowlist[0]->match("accept-language"));
}

TEST(VaryParseAllowlist, MultipleRules) {
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;

  // Matches only "accept".
  const auto& add_new_rule1 = config.mutable_allowed_vary_headers()->Add();
  add_new_rule1->set_exact("accept");

  // Matches only "accept-language".
  const auto& add_new_rule2 = config.mutable_allowed_vary_headers()->Add();
  add_new_rule2->set_exact("accept-language");

  // Matches any string starting with "accept".
  const auto& add_new_rule3 = config.mutable_allowed_vary_headers()->Add();
  add_new_rule3->set_prefix("accept");

  std::vector<Matchers::StringMatcherPtr> allowlist =
      VaryHeader::parseAllowlist(config.allowed_vary_headers());
  ASSERT_EQ(allowlist.size(), 3);

  EXPECT_TRUE(allowlist[0]->match("accept"));
  EXPECT_FALSE(allowlist[0]->match("accept-language"));

  EXPECT_FALSE(allowlist[1]->match("accept"));
  EXPECT_TRUE(allowlist[1]->match("accept-language"));

  EXPECT_TRUE(allowlist[2]->match("accept"));
  EXPECT_TRUE(allowlist[2]->match("accept-language"));
}

envoy::extensions::filters::http::cache::v3alpha::CacheConfig getConfig() {
  // Allows {accept, accept-language, width} to be varied in the tests.
  envoy::extensions::filters::http::cache::v3alpha::CacheConfig config;

  const auto& add_accept = config.mutable_allowed_vary_headers()->Add();
  add_accept->set_exact("accept");

  const auto& add_accept_language = config.mutable_allowed_vary_headers()->Add();
  add_accept_language->set_exact("accept-language");

  const auto& add_width = config.mutable_allowed_vary_headers()->Add();
  add_width->set_exact("width");

  return config;
}

class VaryHeaderTest : public testing::Test {
protected:
  VaryHeaderTest() : vary_allowlist_(getConfig().allowed_vary_headers()) {}

  VaryHeader vary_allowlist_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
};

TEST_F(VaryHeaderTest, IsAllowedNull) { ASSERT_TRUE(vary_allowlist_.isAllowed(response_headers_)); }

TEST_F(VaryHeaderTest, IsAllowedEmpty) {
  response_headers_.addCopy("vary", "");
  ASSERT_TRUE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, IsAllowedSingle) {
  response_headers_.addCopy("vary", "accept");
  ASSERT_TRUE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, IsAllowedMultiple) {
  response_headers_.addCopy("vary", "accept");
  ASSERT_TRUE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, NotIsAllowedStar) {
  // Should never be allowed, regardless of the allowlist.
  response_headers_.addCopy("vary", "*");
  ASSERT_FALSE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, NotIsAllowedSingle) {
  response_headers_.addCopy("vary", "wrong-header");
  ASSERT_FALSE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, NotIsAllowedMixed) {
  response_headers_.addCopy("vary", "accept, wrong-header");
  ASSERT_FALSE(vary_allowlist_.isAllowed(response_headers_));
}

TEST_F(VaryHeaderTest, PossibleVariedHeadersEmpty) {
  Http::HeaderMapPtr result = vary_allowlist_.possibleVariedHeaders(request_headers_);

  EXPECT_FALSE(result->get(Http::LowerCaseString("accept")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("accept-language")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("width")));
}

TEST_F(VaryHeaderTest, PossibleVariedHeadersNoOverlap) {
  request_headers_.addCopy("abc", "123");
  Http::HeaderMapPtr result = vary_allowlist_.possibleVariedHeaders(request_headers_);

  EXPECT_FALSE(result->get(Http::LowerCaseString("accept")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("accept-language")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("width")));
}

TEST_F(VaryHeaderTest, PossibleVariedHeadersOverlap) {
  request_headers_.addCopy("abc", "123");
  request_headers_.addCopy("accept", "image/*");
  Http::HeaderMapPtr result = vary_allowlist_.possibleVariedHeaders(request_headers_);

  std::vector<absl::string_view> values;
  Http::HeaderUtility::getAllOfHeader(*result, "accept", values);
  ASSERT_EQ(values.size(), 1);
  EXPECT_EQ(values[0], "image/*");

  EXPECT_FALSE(result->get(Http::LowerCaseString("accept-language")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("width")));
}

TEST_F(VaryHeaderTest, PossibleVariedHeadersMultiValues) {
  request_headers_.addCopy("accept", "image/*");
  request_headers_.addCopy("accept", "text/html");
  Http::HeaderMapPtr result = vary_allowlist_.possibleVariedHeaders(request_headers_);

  std::vector<absl::string_view> values;
  Http::HeaderUtility::getAllOfHeader(*result, "accept", values);
  ASSERT_EQ(values.size(), 2);
  EXPECT_EQ(values[0], "image/*");
  EXPECT_EQ(values[1], "text/html");

  EXPECT_FALSE(result->get(Http::LowerCaseString("accept-language")));
  EXPECT_FALSE(result->get(Http::LowerCaseString("width")));
}

TEST_F(VaryHeaderTest, PossibleVariedHeadersMultiHeaders) {
  request_headers_.addCopy("accept", "image/*");
  request_headers_.addCopy("accept-language", "en-US");
  Http::HeaderMapPtr result = vary_allowlist_.possibleVariedHeaders(request_headers_);

  std::vector<absl::string_view> values;
  Http::HeaderUtility::getAllOfHeader(*result, "accept", values);
  ASSERT_EQ(values.size(), 1);
  EXPECT_EQ(values[0], "image/*");

  Http::HeaderUtility::getAllOfHeader(*result, "accept-language", values);
  ASSERT_EQ(values.size(), 2);
  EXPECT_EQ(values[1], "en-US");

  EXPECT_FALSE(result->get(Http::LowerCaseString("width")));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
