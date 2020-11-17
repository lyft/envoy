#include "extensions/filters/http/kill_request/kill_request_config.h"

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {

using testing::_;

TEST(KillRequestConfigTest, KillRequestFilterWithCorrectProto) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  KillRequestFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(kill_request, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(KillRequestConfigTest, KillRequestFilterWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  KillRequestFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(
      *factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// Test that the deprecated extension name still functions.
TEST(KillRequestConfigTest,
     DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.kill_request";

  ASSERT_NE(nullptr, Registry::FactoryRegistry<
                         Server::Configuration::NamedHttpFilterConfigFactory>::
                         getFactory(deprecated_name));
}

}  // namespace
}  // namespace KillRequest
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
