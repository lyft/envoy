#include "extensions/filters/network/tcp_proxy/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

TEST(ConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(ConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::tcp_proxy::v2::TcpProxy(), context),
               ProtoValidationException);
}

// Test that a minimal TcpProxy v2 config works.
TEST(ConfigTest, ConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ConfigFactory factory;
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy config =
      *dynamic_cast<envoy::config::filter::network::tcp_proxy::v2::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
