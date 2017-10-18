#include "test/integration/autonomous_upstream.h"

namespace Envoy {

namespace {

void HeaderToInt(const char header_name[], int32_t& return_int, Http::TestHeaderMapImpl& headers) {
  std::string header_value = headers.get_(header_name);
  if (!header_value.empty()) {
    return_int = atoi(header_value.c_str());
  }
}

} // namespace

const char AutonomousStream::RESPONSE_SIZE_BYTES[] = "response_size_bytes";
const char AutonomousStream::EXPECT_REQUEST_SIZE_BYTES[] = "expect_request_size_bytes";
const char AutonomousStream::RESET_AFTER_REQUEST[] = "reset_after_request";

// For now, assert all streams which are started are completed.
// Support for incomplete streams can be added when needed.
AutonomousStream::~AutonomousStream() { RELEASE_ASSERT(complete()); }

// By default, automatically send a response when the request is complete.
void AutonomousStream::setEndStream(bool end_stream) {
  FakeStream::setEndStream(end_stream);
  if (end_stream) {
    sendResponse();
  }
}

// Check all the special headers and send a customized response based on them.
void AutonomousStream::sendResponse() {
  Http::TestHeaderMapImpl headers(*headers_);

  int32_t request_body_length = -1;
  HeaderToInt(EXPECT_REQUEST_SIZE_BYTES, request_body_length, headers);
  if (request_body_length > 0) {
    EXPECT_EQ(request_body_length, bodyLength());
  }

  if (!headers.get_(RESET_AFTER_REQUEST).empty()) {
    encodeResetStream();
    return;
  }

  int32_t response_body_length = 10;
  HeaderToInt(RESPONSE_SIZE_BYTES, response_body_length, headers);

  encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  encodeData(response_body_length, true);
}

Http::StreamDecoder& AutonomousHttpConnection::newStream(Http::StreamEncoder& response_encoder) {
  std::unique_lock<std::mutex> lock(lock_);
  auto stream = new AutonomousStream(*this, response_encoder);
  streams_.push_back(FakeStreamPtr{stream});
  return *(stream);
}

bool AutonomousUpstream::createFilterChain(Network::Connection& connection) {
  std::unique_lock<std::mutex> lock(lock_);
  AutonomousHttpConnectionPtr http_connection(new AutonomousHttpConnection(
      QueuedConnectionWrapperPtr{new QueuedConnectionWrapper(connection, true)}, stats_store_,
      http_type_));
  http_connection->initialize();
  http_connections_.push_back(std::move(http_connection));
  return true;
}

} // namespace Envoy
