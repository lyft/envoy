#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/tracing/zipkin/tracer.h"
#include "common/tracing/zipkin/util.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::NiceMock;

namespace Zipkin {

class TestReporterImpl : public Reporter {
public:
  TestReporterImpl(int value) : value_(value) {}
  void reportSpan(const Span& span) { reported_spans_.push_back(span); }
  int getValue() { return value_; }
  std::vector<Span>& reportedSpans() { return reported_spans_; }

private:
  int value_;
  std::vector<Span> reported_spans_;
};

TEST(ZipkinTracerTest, spanCreation) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Address::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator);
  NiceMock<MockSystemTimeSource> mock_start_time;
  SystemTime timestamp = mock_start_time.currentTime();

  // ==============
  // Test the creation of a root span --> CS
  // ==============
  ON_CALL(random_generator, random()).WillByDefault(Return(1000));
  SpanPtr root_span = tracer.startSpan("my_span", timestamp);

  EXPECT_EQ("my_span", root_span->name());
  EXPECT_NE(0LL, root_span->startTime());
  EXPECT_NE(0ULL, root_span->traceId());            // trace id must be set
  EXPECT_EQ(root_span->traceId(), root_span->id()); // span id and trace id must be the same
  EXPECT_FALSE(root_span->isSetParentId());         // no parent set
  // span's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      root_span->timestamp());

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, root_span->annotations().size());
  Annotation ann = root_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), root_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(root_span->isSetDuration());

  // ==============
  // Test the creation of a shared-context span --> SR
  // ==============

  SpanContext root_span_context(*root_span);
  SpanPtr server_side_shared_context_span =
      tracer.startSpan("my_span", timestamp, root_span_context);

  EXPECT_NE(0LL, server_side_shared_context_span->startTime());

  // span name should NOT be set (it was set in the CS side)
  EXPECT_EQ("", server_side_shared_context_span->name());

  // trace id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->traceId(), server_side_shared_context_span->traceId());

  // span id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->id(), server_side_shared_context_span->id());

  // The parent should be the same as in the CS side (none in this case)
  EXPECT_FALSE(server_side_shared_context_span->isSetParentId());

  // span timestamp should not be set (it was set in the CS side)
  EXPECT_FALSE(server_side_shared_context_span->isSetTimestamp());

  // An SR annotation must have been added
  EXPECT_EQ(1ULL, server_side_shared_context_span->annotations().size());
  ann = server_side_shared_context_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), server_side_shared_context_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(server_side_shared_context_span->isSetDuration());

  // ==============
  // Test the creation of a child span --> CS
  // ==============
  ON_CALL(random_generator, random()).WillByDefault(Return(2000));
  SpanContext server_side_context(*server_side_shared_context_span);
  SpanPtr child_span = tracer.startSpan("my_child_span", timestamp, server_side_context);

  EXPECT_EQ("my_child_span", child_span->name());
  EXPECT_NE(0LL, child_span->startTime());

  // trace id must be retained
  EXPECT_NE(0ULL, child_span->traceId());
  EXPECT_EQ(server_side_shared_context_span->traceId(), child_span->traceId());

  // span id and trace id must NOT be the same
  EXPECT_NE(child_span->traceId(), child_span->id());

  // parent should be the previous span
  EXPECT_TRUE(child_span->isSetParentId());
  EXPECT_EQ(server_side_shared_context_span->id(), child_span->parentId());

  // span's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      child_span->timestamp());

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, child_span->annotations().size());
  ann = child_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());
  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), child_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(child_span->isSetDuration());

  // ==============
  // Test the creation of a shared-context span with a parent --> SR
  // ==============

  const std::string generated_parent_id = Hex::uint64ToHex(Util::generateRandom64());
  const std::string modified_root_span_context_str =
      root_span_context.traceIdAsHexString() + ";" + root_span_context.idAsHexString() + ";" +
      generated_parent_id + ";" + ZipkinCoreConstants::get().CLIENT_SEND;
  SpanContext modified_root_span_context;
  modified_root_span_context.populateFromString(modified_root_span_context_str);
  SpanPtr new_shared_context_span =
      tracer.startSpan("new_shared_context_span", timestamp, modified_root_span_context);
  EXPECT_NE(0LL, new_shared_context_span->startTime());

  // span name should NOT be set (it was set in the CS side)
  EXPECT_EQ("", new_shared_context_span->name());

  // trace id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->traceId(), new_shared_context_span->traceId());

  // span id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->id(), new_shared_context_span->id());

  // The parent should be the same as in the CS side
  EXPECT_TRUE(new_shared_context_span->isSetParentId());
  EXPECT_EQ(modified_root_span_context.parent_id(), new_shared_context_span->parentId());

  // span timestamp should not be set (it was set in the CS side)
  EXPECT_FALSE(new_shared_context_span->isSetTimestamp());

  // An SR annotation must have been added
  EXPECT_EQ(1ULL, new_shared_context_span->annotations().size());
  ann = new_shared_context_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), new_shared_context_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(new_shared_context_span->isSetDuration());
}

TEST(ZipkinTracerTest, finishSpan) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Address::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator);
  NiceMock<MockSystemTimeSource> mock_start_time;
  SystemTime timestamp = mock_start_time.currentTime();

  // ==============
  // Test finishing a span containing a CS annotation
  // ==============

  // Creates a root-span with a CS annotation
  SpanPtr span = tracer.startSpan("my_span", timestamp);

  // Finishing a root span with a CS annotation must add a CR annotation
  span->finish();
  EXPECT_EQ(2ULL, span->annotations().size());

  // Check the CS annotation added at span-creation time
  Annotation ann = span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());

  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // Check the CR annotation added when ending the span
  ann = span->annotations()[1];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_RECV, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // ==============
  // Test finishing a span containing an SR annotation
  // ==============

  SpanContext context(*span);
  SpanPtr server_side = tracer.startSpan("my_span", timestamp, context);

  // Associate a reporter with the tracer
  TestReporterImpl* reporter_object = new TestReporterImpl(135);
  ReporterPtr reporter_ptr(reporter_object);
  tracer.setReporter(std::move(reporter_ptr));

  // Finishing a server-side span with an SR annotation must add an SS annotation
  server_side->finish();
  EXPECT_EQ(2ULL, server_side->annotations().size());

  // Test if the reporter's reportSpan method was actually called upon finishing the span
  EXPECT_EQ(1ULL, reporter_object->reportedSpans().size());

  // Check the SR annotation added at span-creation time
  ann = server_side->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // Check the SS annotation added when ending the span
  ann = server_side->annotations()[1];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_SEND, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());
}
} // Zipkin
