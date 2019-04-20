#include "extensions/filters/network/kafka/kafka_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const RequestParserResolver& RequestParserResolver::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(RequestParserResolver);
}

ParseResponse RequestStartParser::parse(absl::string_view& data) {
  request_length_.feed(data);
  if (request_length_.ready()) {
    context_->remaining_request_size_ = request_length_.get();
    return ParseResponse::nextParser(std::make_shared<RequestHeaderParser>(context_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

ParseResponse RequestHeaderParser::parse(absl::string_view&) {
  return ParseResponse::stillWaiting();
}

ParseResponse SentinelParser::parse(absl::string_view& data) {
  const size_t min = std::min<size_t>(context_->remaining_request_size_, data.size());
  data = {data.data() + min, data.size() - min};
  context_->remaining_request_size_ -= min;
  if (0 == context_->remaining_request_size_) {
    return ParseResponse::parsedMessage(
        std::make_shared<UnknownRequest>(context_->request_header_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
