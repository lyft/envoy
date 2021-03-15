#include "common/config/utility.h"

#include "extensions/matching/generic_inputs/environment/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

TEST(ConfigTest, TestConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.generic_inputs.environment.v3.Environment
        name: foo
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  Config factory;
  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);

  {
    auto input = factory.createGenericDataInput(*message, context);
    EXPECT_NE(nullptr, input);
    EXPECT_EQ(input->get(), absl::nullopt);
  }

  TestEnvironment::setEnvVar("foo", "bar", 1);
  {
    auto input = factory.createGenericDataInput(*message, context);
    EXPECT_NE(nullptr, input);
    EXPECT_EQ(input->get(), absl::make_optional("bar"));
  }

  TestEnvironment::unsetEnvVar("foo");
}

} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy