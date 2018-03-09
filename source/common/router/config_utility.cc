#include "common/router/config_utility.h"

#include <regex>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Router {

bool ConfigUtility::QueryParameterMatcher::matches(
    const Http::Utility::QueryParams& request_query_params) const {
  auto query_param = request_query_params.find(name_);
  if (query_param == request_query_params.end()) {
    return false;
  } else if (is_regex_) {
    return std::regex_match(query_param->second, regex_pattern_);
  } else if (value_.length() == 0) {
    return true;
  } else {
    return (value_ == query_param->second);
  }
}

Upstream::ResourcePriority
ConfigUtility::parsePriority(const envoy::api::v2::core::RoutingPriority& priority) {
  switch (priority) {
  case envoy::api::v2::core::RoutingPriority::DEFAULT:
    return Upstream::ResourcePriority::Default;
  case envoy::api::v2::core::RoutingPriority::HIGH:
    return Upstream::ResourcePriority::High;
  default:
    NOT_IMPLEMENTED;
  }
}

bool ConfigUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers) {
  bool matches = true;

  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      const Http::HeaderEntry* header = request_headers.get(cfg_header_data.name_);

      switch (cfg_header_data.header_match_type_) {
      case HeaderMatchType::Value:
        matches &= (header != nullptr) && (cfg_header_data.value_.empty() ||
                                           (header->value() == cfg_header_data.value_.c_str()));
        break;

      case HeaderMatchType::Regex:
        matches &= (header != nullptr) &&
                   std::regex_match(header->value().c_str(), cfg_header_data.regex_pattern_);
        break;

      case HeaderMatchType::Range:
        int64_t header_value = 0;
        matches &= (header != nullptr) &&
                   StringUtil::atol(header->value().c_str(), header_value, 10) &&
                   header_value >= cfg_header_data.range_.start() &&
                   header_value < cfg_header_data.range_.end();
        break;
      }

      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

bool ConfigUtility::matchQueryParams(
    const Http::Utility::QueryParams& query_params,
    const std::vector<QueryParameterMatcher>& config_query_params) {
  for (const auto& config_query_param : config_query_params) {
    if (!config_query_param.matches(query_params)) {
      return false;
    }
  }

  return true;
}

Http::Code ConfigUtility::parseRedirectResponseCode(
    const envoy::api::v2::route::RedirectAction::RedirectResponseCode& code) {
  switch (code) {
  case envoy::api::v2::route::RedirectAction::MOVED_PERMANENTLY:
    return Http::Code::MovedPermanently;
  case envoy::api::v2::route::RedirectAction::FOUND:
    return Http::Code::Found;
  case envoy::api::v2::route::RedirectAction::SEE_OTHER:
    return Http::Code::SeeOther;
  case envoy::api::v2::route::RedirectAction::TEMPORARY_REDIRECT:
    return Http::Code::TemporaryRedirect;
  case envoy::api::v2::route::RedirectAction::PERMANENT_REDIRECT:
    return Http::Code::PermanentRedirect;
  default:
    NOT_IMPLEMENTED;
  }
}

Optional<Http::Code>
ConfigUtility::parseDirectResponseCode(const envoy::api::v2::route::Route& route) {
  if (route.has_redirect()) {
    return parseRedirectResponseCode(route.redirect().response_code());
  } else if (route.has_direct_response()) {
    return static_cast<Http::Code>(route.direct_response().status());
  }
  return Optional<Http::Code>();
}

std::string ConfigUtility::parseDirectResponseBody(const envoy::api::v2::route::Route& route) {
  static const ssize_t MaxBodySize = 4096;
  if (!route.has_direct_response() || !route.direct_response().has_body()) {
    return EMPTY_STRING;
  }
  const auto& body = route.direct_response().body();
  const std::string filename = body.filename();
  if (!filename.empty()) {
    if (!Filesystem::fileExists(filename)) {
      throw EnvoyException(fmt::format("response body file {} does not exist", filename));
    }
    ssize_t size = Filesystem::fileSize(filename);
    if (size < 0) {
      throw EnvoyException(fmt::format("cannot determine size of response body file {}", filename));
    }
    if (size > MaxBodySize) {
      throw EnvoyException(fmt::format("response body file {} size is {} bytes; maximum is {}",
                                       filename, size, MaxBodySize));
    }
    return Filesystem::fileReadToEnd(filename);
  }
  const std::string inline_body(body.inline_bytes().empty() ? body.inline_string()
                                                            : body.inline_bytes());
  if (inline_body.length() > MaxBodySize) {
    throw EnvoyException(fmt::format("response body size is {} bytes; maximum is {}",
                                     inline_body.length(), MaxBodySize));
  }
  return inline_body;
}

Http::Code ConfigUtility::parseClusterNotFoundResponseCode(
    const envoy::api::v2::route::RouteAction::ClusterNotFoundResponseCode& code) {
  switch (code) {
  case envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE:
    return Http::Code::ServiceUnavailable;
  case envoy::api::v2::route::RouteAction::NOT_FOUND:
    return Http::Code::NotFound;
  default:
    NOT_IMPLEMENTED;
  }
}

} // namespace Router
} // namespace Envoy
