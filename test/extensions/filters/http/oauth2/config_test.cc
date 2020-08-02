#include <memory>
#include <string>

#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.h"

#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/oauth2/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

using testing::NiceMock;

TEST(ConfigTest, CreateFilter) {
  OAuth2Config config;

  const std::string yaml = R"EOF(
config:
    token_endpoint:
      cluster: foo
      uri: oauth.com/token
      timeout: 3s
    authorization_endpoint: https://oauth.com/oauth/authorize/
    redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
    signout_path: 
      path:
        exact: /signout
    )EOF";

  envoy::extensions::filters::http::oauth2::v3alpha::OAuth2 proto_config;
  MessageUtil::loadFromYaml(yaml, proto_config, ProtobufMessage::getStrictValidationVisitor());
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  auto cb = config.createFilterFactoryFromProtoTyped(proto_config, "whatever", factory_context);

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  cb(filter_callbacks);
}

TEST(ConfigTest, CreateFilterMissingConfig) {
  OAuth2Config config;

  envoy::extensions::filters::http::oauth2::v3alpha::OAuth2 proto_config;

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW_WITH_MESSAGE(
      config.createFilterFactoryFromProtoTyped(proto_config, "whatever", factory_context),
      EnvoyException, "config must be present for global config");
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy