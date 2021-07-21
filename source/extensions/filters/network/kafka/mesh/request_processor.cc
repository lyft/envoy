#include "source/extensions/filters/network/kafka/mesh/request_processor.h"

#include "envoy/common/exception.h"

#include "source/extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"
#include "source/extensions/filters/network/kafka/mesh/command_handlers/metadata.h"
#include "source/extensions/filters/network/kafka/mesh/command_handlers/produce.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RequestProcessor::RequestProcessor(AbstractRequestListener& origin,
                                   const UpstreamKafkaConfiguration& configuration)
    : origin_{origin}, configuration_{configuration} {}

// Helper function. Throws a nice message. Filter will react by closing the connection.
static void throwOnUnsupportedRequest(const std::string& reason, const RequestHeader& header) {
  throw EnvoyException(absl::StrCat(reason, " Kafka request (key=", header.api_key_, ", version=",
                                    header.api_version_, ", cid=", header.correlation_id_));
}

void RequestProcessor::onMessage(AbstractRequestSharedPtr arg) {
  switch (arg->request_header_.api_key_) {
  case /* Produce */ 0:
    process(std::dynamic_pointer_cast<Request<ProduceRequest>>(arg));
    break;
  case /* Metadata */ 3:
    process(std::dynamic_pointer_cast<Request<MetadataRequest>>(arg));
    break;
  case /* ApiVersions */ 18:
    process(std::dynamic_pointer_cast<Request<ApiVersionsRequest>>(arg));
    break;
  default:
    // We got something else than typical Produce request.
    throwOnUnsupportedRequest("unsupported (bad client API invoked?)", arg->request_header_);
    break;
  } // switch
}

// We got something that the parser could not handle.
void RequestProcessor::onFailedParse(RequestParseFailureSharedPtr arg) {
  throwOnUnsupportedRequest("unknown", arg->request_header_);
}

void RequestProcessor::process(const std::shared_ptr<Request<ProduceRequest>> request) const {
  auto res = std::make_shared<ProduceRequestHolder>(origin_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<MetadataRequest>> request) const {
  auto res = std::make_shared<MetadataRequestHolder>(origin_, configuration_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<ApiVersionsRequest>> request) const {
  auto res = std::make_shared<ApiVersionsRequestHolder>(origin_, request);
  origin_.onRequest(res);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
