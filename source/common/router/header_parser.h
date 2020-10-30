#pragma once

#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"

#include "common/protobuf/protobuf.h"
#include "common/router/header_formatter.h"

namespace Envoy {
namespace Router {

class HeaderParser;
using HeaderParserPtr = std::unique_ptr<HeaderParser>;

/**
 * HeaderParser manipulates Http::HeaderMap instances. Headers to be added are pre-parsed to select
 * between a constant value implementation and a dynamic value implementation based on
 * StreamInfo::StreamInfo fields.
 */
class HeaderParser {
public:
  /*
   * @param headers_to_add defines the headers to add during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr configure(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers_to_add);

  /*
   * @param headers_to_add defines headers to add during calls to evaluateHeaders.
   * @param append defines whether headers will be appended or replaced.
   * @return HeaderParserPtr a configured HeaderParserPtr.
   */
  static HeaderParserPtr
  configure(const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers_to_add,
            bool append);

  /*
   * @param headers_to_add defines headers to add during calls to evaluateHeaders
   * @param headers_to_remove defines headers to remove during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr configure(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers_to_add,
      const Protobuf::RepeatedPtrField<std::string>& headers_to_remove);

  void evaluateHeaders(Http::HeaderMap& headers, const StreamInfo::StreamInfo& stream_info) const;
  void evaluateHeaders(Http::HeaderMap& headers, const StreamInfo::StreamInfo* stream_info) const;

protected:
  HeaderParser() = default;

private:
  struct HeadersToAddEntry {
    HeaderFormatterPtr formatter_;
    const std::string original_value_;
  };

  void evaluateHeader(const Http::LowerCaseString& header_key,
                      const HeadersToAddEntry& header_entry, Http::HeaderMap& headers,
                      const StreamInfo::StreamInfo* stream_info) const;

  std::vector<std::pair<Http::LowerCaseString, HeadersToAddEntry>> headers_to_add_;
  std::vector<Http::LowerCaseString> headers_to_remove_;
  std::vector<std::pair<Http::LowerCaseString, HeadersToAddEntry>> tokenized_headers_to_add_;
};

} // namespace Router
} // namespace Envoy
