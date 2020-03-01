#include <string>

#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"
#include "extensions/tracers/xray/xray_tracer_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {

class XRayDriverTest : public ::testing::Test {
public:
  const std::string operation_name_ = "test_operation_name";
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Tracing::MockConfig> tracing_config_;
  Http::TestRequestHeaderMapImpl request_headers_{
      {":authority", "api.amazon.com"}, {":path", "/"}, {":method", "GET"}};
};

TEST_F(XRayDriverTest, XRayTraceHeaderNotSampled) {
  request_headers_.addCopy(XRayTraceHeader, "Root=1-272793;Parent=5398ad8;Sampled=0");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};
  Envoy::SystemTime start_time;
  auto span = driver.startSpan(tracing_config_, request_headers_, operation_name_, start_time,
                               tracing_decision);
  ASSERT_NE(span, nullptr);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_FALSE(xray_span->sampled());
}

TEST_F(XRayDriverTest, XRayTraceHeaderSampled) {
  request_headers_.addCopy(XRayTraceHeader, "Root=1-272793;Parent=5398ad8;Sampled=1");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};
  Envoy::SystemTime start_time;
  auto span = driver.startSpan(tracing_config_, request_headers_, operation_name_, start_time,
                               tracing_decision);
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, XRayTraceHeaderSamplingUnknown) {
  request_headers_.addCopy(XRayTraceHeader, "Root=1-272793;Parent=5398ad8");

  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};
  Envoy::SystemTime start_time;
  auto span = driver.startSpan(tracing_config_, request_headers_, operation_name_, start_time,
                               tracing_decision);
  // sampling should fall back to the default manifest since:
  // a) there is sampling decision in the X-Ray header
  // b) there are no sampling rules passed, so the default rules apply (1 req/sec and 5% after that
  // within that second)
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, NoXRayTracerHeader) {
  XRayConfiguration config{"" /*daemon_endpoint*/, "test_segment_name", "" /*sampling_rules*/};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, false /*sampled*/};
  Envoy::SystemTime start_time;
  auto span = driver.startSpan(tracing_config_, request_headers_, operation_name_, start_time,
                               tracing_decision);
  // sampling should fall back to the default manifest since:
  // a) there is no X-Ray header to determine the sampling decision
  // b) there are no sampling rules passed, so the default rules apply (1 req/sec and 5% after that
  // within that second)
  ASSERT_NE(span, nullptr);
}

TEST_F(XRayDriverTest, EmptySegmentNameDefaultToClusterName) {
  const std::string cluster_name = "FooBar";
  EXPECT_CALL(context_.server_factory_context_->local_info_, clusterName())
      .WillRepeatedly(ReturnRef(cluster_name));
  XRayConfiguration config{"" /*daemon_endpoint*/, "", "" /*sampling_rules*/};
  Driver driver(config, context_);

  Tracing::Decision tracing_decision{Tracing::Reason::Sampling, true /*sampled*/};
  Envoy::SystemTime start_time;
  auto span = driver.startSpan(tracing_config_, request_headers_, operation_name_, start_time,
                               tracing_decision);
  auto* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_STREQ(xray_span->name().c_str(), cluster_name.c_str());
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
