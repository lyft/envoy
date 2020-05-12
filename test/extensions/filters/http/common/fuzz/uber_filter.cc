#include "test/extensions/filters/http/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

UberFilterFuzzer::UberFilterFuzzer() {
  // Need to set for both a decoder filter and an encoder/decoder filter.
  ON_CALL(filter_callback_, addStreamDecoderFilter(_))
      .WillByDefault(Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
        filter_ = filter;
        filter_->setDecoderFilterCallbacks(callbacks_);
      }));
  ON_CALL(filter_callback_, addStreamFilter(_))
      .WillByDefault(Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
        filter_ = filter;
        filter_->setDecoderFilterCallbacks(callbacks_);
      }));
  // Set expectations for particular filters that may get fuzzed.
  perFilterSetup();
}

std::vector<std::string> UberFilterFuzzer::parseHttpData(const test::fuzz::HttpData& data) {
  std::vector<std::string> data_chunks;

  if (data.has_http_body()) {
    for (const auto& http_data : data.http_body().data()) {
      data_chunks.push_back(http_data.data());
    }
  } else if (data.has_proto_body()) {
    const std::string serialized = data.proto_body().message().value();
    data_chunks = absl::StrSplit(serialized, absl::ByLength(data.proto_body().chunk_size()));
  }

  return data_chunks;
}

void UberFilterFuzzer::decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data) {
  bool end_stream = false;

  auto headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(data.headers());
  if (headers.Path() == nullptr) {
    headers.setPath("/foo");
  }
  if (headers.Method() == nullptr) {
    headers.setMethod("GET");
  }
  if (headers.Host() == nullptr) {
    headers.setHost("foo.com");
  }

  if (data.body_case() == test::fuzz::HttpData::BODY_NOT_SET && !data.has_trailers()) {
    end_stream = true;
  }
  ENVOY_LOG_MISC(debug, "Decoding headers (end_stream={}): {} ", end_stream,
                 data.headers().DebugString());
  const auto& headersStatus = filter->decodeHeaders(headers, end_stream);
  if (headersStatus != Http::FilterHeadersStatus::Continue &&
      headersStatus != Http::FilterHeadersStatus::StopIteration) {
    return;
  }

  const std::vector<std::string> data_chunks = parseHttpData(data);
  for (size_t i = 0; i < data_chunks.size(); i++) {
    if (!data.has_trailers() && i == data_chunks.size() - 1) {
      end_stream = true;
    }
    Buffer::OwnedImpl buffer(data_chunks[i]);
    ENVOY_LOG_MISC(debug, "Decoding data (end_stream={}): {} ", end_stream, buffer.toString());
    if (filter->decodeData(buffer, end_stream) != Http::FilterDataStatus::Continue) {
      return;
    }
  }

  if (data.has_trailers()) {
    ENVOY_LOG_MISC(debug, "Decoding trailers: {} ", data.trailers().DebugString());
    auto trailers = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
    filter->decodeTrailers(trailers);
  }
}

void UberFilterFuzzer::fuzz(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
        proto_config,
    const test::fuzz::HttpData& data) {
  try {
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedHttpFilterConfigFactory>(proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    // Clean-up config with filter-specific logic.
    cleanFuzzedConfig(proto_config.name(), message.get());
    cb_ = factory.createFilterFactoryFromProto(*message, "stats", factory_context_);
    cb_(filter_callback_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
    return;
  }

  decode(filter_.get(), data);
  reset();
}

void UberFilterFuzzer::guideAnyProtoType(test::fuzz::HttpData* mutable_data, int seed) {
  // These types are request/response from the test Bookstore service
  // for the gRPC Transcoding filter.
  static const std::vector<std::string> expected_types = {
      "type.googleapis.com/bookstore.ListShelvesResponse",
      "type.googleapis.com/bookstore.CreateShelfRequest",
      "type.googleapis.com/bookstore.GetShelfRequest",
      "type.googleapis.com/bookstore.DeleteShelfRequest",
      "type.googleapis.com/bookstore.ListBooksRequest",
      "type.googleapis.com/bookstore.CreateBookRequest",
      "type.googleapis.com/bookstore.GetBookRequest",
      "type.googleapis.com/bookstore.UpdateBookRequest",
      "type.googleapis.com/bookstore.DeleteBookRequest",
      "type.googleapis.com/bookstore.GetAuthorRequest",
      "type.googleapis.com/bookstore.EchoBodyRequest",
      "type.googleapis.com/bookstore.EchoStructReqResp",
      "type.googleapis.com/bookstore.Shelf",
      "type.googleapis.com/bookstore.Book",
      "type.googleapis.com/google.protobuf.Empty",
      "type.googleapis.com/google.api.HttpBody",
  };
  ProtobufWkt::Any* mutable_any = mutable_data->mutable_proto_body()->mutable_message();
  const std::string& type_url = expected_types[(seed / 2) % expected_types.size()];
  mutable_any->set_type_url(type_url);
}

void UberFilterFuzzer::reset() {
  if (filter_ != nullptr) {
    filter_->onDestroy();
  }
  filter_.reset();
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
