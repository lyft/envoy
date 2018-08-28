#include "server/config_validation/async_client.h"

namespace Envoy {
namespace Http {

ValidationAsyncClient::ValidationAsyncClient(TimeSource& time_source) : dispatcher_(time_source) {}

AsyncClient::Request*
ValidationAsyncClient::send(MessagePtr&&, Callbacks&,
                            const absl::optional<std::chrono::milliseconds>&) {
  return nullptr;
}

AsyncClient::Stream* ValidationAsyncClient::start(StreamCallbacks&,
                                                  const absl::optional<std::chrono::milliseconds>&,
                                                  bool) {
  return nullptr;
}

} // namespace Http
} // namespace Envoy
