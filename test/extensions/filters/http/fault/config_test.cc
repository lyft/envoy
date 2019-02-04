#include "envoy/config/filter/http/fault/v2/fault.pb.validate.h"

#include "extensions/filters/http/fault/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

TEST_F(TestBase, FaultFilterConfigTest_ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::config::filter::http::fault::v2::HTTPFault fault;
  fault.mutable_abort();
  EXPECT_THROW(FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context),
               ProtoValidationException);
}

TEST_F(TestBase, FaultFilterConfigTest_FaultFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "delay" : {
      "type" : "fixed",
      "fixed_delay_percent" : 100,
      "fixed_duration_ms" : 5000
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST_F(TestBase, FaultFilterConfigTest_FaultFilterCorrectProto) {
  envoy::config::filter::http::fault::v2::HTTPFault config{};
  config.mutable_delay()->mutable_percentage()->set_numerator(100);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(5);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST_F(TestBase, FaultFilterConfigTest_FaultFilterEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
