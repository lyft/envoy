#include "common/network/utility.h"

#include "extensions/tracers/zipkin/span_buffer.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

Endpoint createEndpoint() {
  Endpoint endpoint;
  endpoint.setAddress(Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 8080, false));
  endpoint.setServiceName("service1");
  return endpoint;
}

Annotation createAnnotation(const absl::string_view value) {
  Annotation annotation;
  annotation.setValue(value.data());
  annotation.setTimestamp(1566058071601051);
  annotation.setEndpoint(createEndpoint());
  return annotation;
}

BinaryAnnotation createTag() {
  BinaryAnnotation tag;
  tag.setKey("component");
  tag.setValue("proxy");
  return tag;
}

Span createSpan(const std::vector<absl::string_view>& annotation_values) {
  DangerousDeprecatedTestTime test_time;
  Span span = Span(test_time.timeSystem());
  span.setId(1);
  span.setTraceId(1);
  span.setDuration(100);
  std::vector<Annotation> annotations;
  for (absl::string_view value : annotation_values) {
    annotations.push_back(createAnnotation(value));
  }
  span.setAnnotations(annotations);
  span.setBinaryAnnotations({createTag()});
  return span;
}

void expectSerializedBuffer(SpanBuffer& buffer, const bool delay_allocation,
                            const std::vector<std::string>& expected) {
  DangerousDeprecatedTestTime test_time;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());

  if (delay_allocation) {
    EXPECT_FALSE(buffer.addSpan(Span(test_time.timeSystem())));
    buffer.allocateBuffer(2);
  }

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());

  buffer.addSpan(Span(test_time.timeSystem()));
  EXPECT_EQ(1ULL, buffer.pendingSpans());
  EXPECT_EQ(expected.at(0), buffer.serialize());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());

  buffer.addSpan(Span(test_time.timeSystem()));
  buffer.addSpan(Span(test_time.timeSystem()));

  EXPECT_EQ(2ULL, buffer.pendingSpans());
  EXPECT_EQ(expected.at(1), buffer.serialize());

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());
}

template <typename Type> std::string serializedMessageToJson(const std::string& serialized) {
  Type message;
  message.ParseFromString(serialized);
  std::string json;
  Protobuf::util::MessageToJsonString(message, &json);
  return json;
}

TEST(ZipkinSpanBufferTest, ConstructBuffer) {
  const bool shared = true;
  SpanBuffer buffer1(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON_V1, shared);
  const bool delay_allocation = true;
  expectSerializedBuffer(buffer1, delay_allocation,
                         {"[{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "}]",
                          "["
                          "{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "},"
                          "{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "}"
                          "]"});

  SpanBuffer buffer2(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON_V1, shared, 2);
  expectSerializedBuffer(buffer2, !delay_allocation,
                         {"[{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "}]",
                          "["
                          "{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "},"
                          "{"
                          R"("traceId":"0000000000000000",)"
                          R"("name":"",)"
                          R"("id":"0000000000000000",)"
                          R"("annotations":[],)"
                          R"("binaryAnnotations":[])"
                          "}"
                          "]"});

  SpanBuffer buffer3(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON, shared);
  expectSerializedBuffer(buffer3, delay_allocation, {"[]", "[]"});
}

TEST(ZipkinSpanBufferTest, SerializeSpan) {
  const bool shared = true;
  SpanBuffer buffer1(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer1.addSpan(createSpan({"cs"}));
  EXPECT_EQ("[{"
            R"("traceId":"0000000000000001",)"
            R"("id":"0000000000000001",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"1.2.3.4",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]",
            buffer1.serialize());

  SpanBuffer buffer2(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer2.addSpan(createSpan({"cs", "sr"}));
  EXPECT_EQ("[{"
            R"("traceId":"0000000000000001",)"
            R"("id":"0000000000000001",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"1.2.3.4",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"0000000000000001",)"
            R"("id":"0000000000000001",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"1.2.3.4",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"},)"
            R"("shared":true)"
            "}]",
            buffer2.serialize());

  SpanBuffer buffer3(envoy::config::trace::v2::ZipkinConfig::HTTP_JSON, !shared, 2);
  buffer3.addSpan(createSpan({"cs", "sr"}));
  EXPECT_EQ("[{"
            R"("traceId":"0000000000000001",)"
            R"("id":"0000000000000001",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"1.2.3.4",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"0000000000000001",)"
            R"("id":"0000000000000001",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"1.2.3.4",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]",
            buffer3.serialize());

  SpanBuffer buffer4(envoy::config::trace::v2::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer4.addSpan(createSpan({"cs"}));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer4.serialize()));

  SpanBuffer buffer5(envoy::config::trace::v2::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer5.addSpan(createSpan({"cs", "sr"}));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"},)"
            R"("shared":true)"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer5.serialize()));

  SpanBuffer buffer6(envoy::config::trace::v2::ZipkinConfig::HTTP_PROTO, !shared, 2);
  buffer6.addSpan(createSpan({"cs", "sr"}));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer6.serialize()));
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
