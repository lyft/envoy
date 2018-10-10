#include "extensions/tracers/dynamic_ot/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {

TEST(DynamicOtTracerConfigTest, DynamicOpentracingHttpTracer) {
  NiceMock<Server::MockInstance> server;
  EXPECT_CALL(server.cluster_manager_, get("fake_cluster"))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));
  ON_CALL(*server.cluster_manager_.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = fmt::sprintf(R"EOF(
  http:
    name: envoy.dynamic.ot
    config:
      library: %s/external/io_opentracing_cpp/mocktracer/libmocktracer_plugin.so
      config:
        output_file: fake_file
  )EOF",
                                               TestEnvironment::runfilesDirectory());
  envoy::config::trace::v2::Tracing configuration;
  MessageUtil::loadFromYaml(yaml_string, configuration);

  DynamicOpenTracingTracerFactory factory;

  const Tracing::HttpTracerPtr tracer = factory.createHttpTracer(configuration, server);
  EXPECT_NE(nullptr, tracer);
}

} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
