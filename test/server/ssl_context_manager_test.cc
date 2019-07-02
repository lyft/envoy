#include "server/ssl_context_manager.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

TEST(SslContextManager, createStub) {
  Event::SimulatedTimeSystem time_system;
  Stats::MockStore scope;
  Ssl::MockClientContextConfig client_config;
  Ssl::MockServerContextConfig server_config;
  std::vector<std::string> server_names;

  Ssl::ContextManagerPtr manager = createContextManager("fake_factory_name", time_system);

  // Check we've created a stub, not real manager.
  EXPECT_EQ(manager->daysUntilFirstCertExpires(), std::numeric_limits<int>::max());
  EXPECT_THROW(manager->createSslClientContext(scope, client_config), EnvoyException);
  EXPECT_THROW(manager->createSslServerContext(scope, server_config, server_names), EnvoyException);
  EXPECT_NO_THROW(manager->iterateContexts([](const Envoy::Ssl::Context&) -> void {}));
}

} // namespace
} // namespace Server
} // namespace Envoy
