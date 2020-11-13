#pragma once

#include "extensions/matching/input/http/inputs.h"
#include "common/http/matching_data.h"
#include "common/matcher/matcher.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/matching/input/v3/http_input.pb.validate.h"

namespace Envoy {
class HttpRequestHeadersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpRequestHeaderInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpRequestHeaders>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_request_headers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpRequestHeaderInput>();
  }
};

class HttpResponseHeadersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpResponseHeaderInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpResponseHeaders>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_response_headers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpResponseHeaderInput>();
  }
};

template <class T> class FixedData : public DataInput<T> {public:
  DataInputGetResult get(const T&) { return {false, ""}; }
};

class FixedDataInputFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData> create(const envoy::config::core::v3::TypedExtensionConfig&) override {
    return std::make_unique<FixedData<Http::HttpMatchingData>>();
  }
  std::string name() const override { return "envoy.matcher.inputs.fixed"; };
  std::string category() const override { return "bkag"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<google::protobuf::Empty>();
  }
};

} // namespace Envoy