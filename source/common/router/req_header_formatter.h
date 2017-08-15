#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>

#include "common/common/logger.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/json/json_loader.h"

#include "api/rds.pb.h"

namespace Envoy {
namespace Router {

/**
 * Interface for all types of header formatters used for custom request headers.
 */
class HeaderFormatter {
public:
  virtual ~HeaderFormatter() {}

  virtual const std::string
  format(const Envoy::Http::AccessLog::RequestInfo& request_info) const PURE;
};

typedef std::unique_ptr<HeaderFormatter> HeaderFormatterPtr;

/**
 * A formatter that expands the request header variable to a value based on info in RequestInfo.
 */
class RequestHeaderFormatter : public HeaderFormatter, Logger::Loggable<Logger::Id::config> {
public:
  RequestHeaderFormatter(const std::string& field_name);

  // HeaderFormatter::format
  const std::string format(const Envoy::Http::AccessLog::RequestInfo& request_info) const override;

private:
  std::function<std::string(const Envoy::Http::AccessLog::RequestInfo&)> field_extractor_;
};

/**
 * Returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {
public:
  PlainHeaderFormatter(const std::string& static_header_value)
      : static_value_(static_header_value){};

  // HeaderFormatter::format
  const std::string format(const Envoy::Http::AccessLog::RequestInfo&) const override {
    return static_value_;
  };

private:
  const std::string static_value_;
};

class RequestHeaderParser;
typedef std::unique_ptr<RequestHeaderParser> RequestHeaderParserPtr;

/**
 * This class will hold the parsing logic required during configuration build and
 * also perform evaluation for the variables at runtime.
 */
class RequestHeaderParser : Logger::Loggable<Logger::Id::config> {
public:
  virtual ~RequestHeaderParser() {}

  static RequestHeaderParserPtr parseRoute(const envoy::api::v2::Route& route);

  static RequestHeaderParserPtr parseVirtualHost(const envoy::api::v2::VirtualHost& virtualHost);

  static RequestHeaderParserPtr
  parseRouteConfiguration(const envoy::api::v2::RouteConfiguration& routeConfig);

  static HeaderFormatterPtr parseInternal(const std::string& format);

  void evaluateRequestHeaders(
      Http::HeaderMap& headers, const Http::AccessLog::RequestInfo& requestInfo,
      const std::list<std::pair<Http::LowerCaseString, std::string>>& requestHeadersToAdd) const;
};

private:
/**
 * Building a map of request header formatters.
 */
std::unordered_map<Http::LowerCaseString, HeaderFormatterPtr, Http::LowerCaseStringHasher>
    header_formatter_map_;
}; // namespace Router

} // namespace Envoy
} // namespace Envoy
