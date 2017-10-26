#include <cstdint>
#include <string>

#include "common/protobuf/utility.h"
#include "common/upstream/cluster_manager_impl.h"

#include "server/configuration_impl.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/utility.h"

#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace ConfigTest {

class ConfigTest {
public:
  ConfigTest(const std::string& file_path) : options_(file_path, Network::Address::IpVersion::v6) {
    ON_CALL(server_, options()).WillByDefault(ReturnRef(options_));
    ON_CALL(server_, random()).WillByDefault(ReturnRef(random_));
    ON_CALL(server_, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
    ON_CALL(server_.api_, fileReadToEnd("lightstep_access_token"))
        .WillByDefault(Return("access_token"));

    envoy::api::v2::Bootstrap bootstrap;
    try {
      printf("NM: calling loadFromFile\n");
      MessageUtil::loadFromFile(file_path, bootstrap);
    } catch (const EnvoyException& e) {
      // TODO(htuch): When v1 is deprecated, make this a warning encouraging config upgrade.
      Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(file_path);
      bootstrap = TestUtility::parseBootstrapFromJson(config_json->asJsonString());
    }
    printf("NM: creating initial_config\n");
    Server::Configuration::InitialImpl initial_config(bootstrap);
    Server::Configuration::MainImpl main_config;

    printf("NM: resetting cluster_manager_factory\n");
    cluster_manager_factory_.reset(new Upstream::ProdClusterManagerFactory(
        server_.runtime(), server_.stats(), server_.threadLocal(), server_.random(),
        server_.dnsResolver(), ssl_context_manager_, server_.dispatcher(), server_.localInfo()));

    ON_CALL(server_, clusterManager()).WillByDefault(Invoke([&]() -> Upstream::ClusterManager& {
      return main_config.clusterManager();
    }));
    ON_CALL(server_, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
    ON_CALL(component_factory_, createFilterFactoryList(_, _))
        .WillByDefault(Invoke([&](const Protobuf::RepeatedPtrField<envoy::api::v2::Filter>& filters,
                                  Server::Configuration::FactoryContext& context)
                                  -> std::vector<Server::Configuration::NetworkFilterFactoryCb> {
          return Server::ProdListenerComponentFactory::createFilterFactoryList_(filters, context);
        }));

    try {
      printf("NM: main_config.initialize\n");
      main_config.initialize(bootstrap, server_, *cluster_manager_factory_);
    } catch (const EnvoyException& ex) {
      ADD_FAILURE() << fmt::format("'{}' config failed. Error: {}", file_path, ex.what());
    }

    server_.thread_local_.shutdownThread();
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  Server::TestOptionsImpl options_;
  std::unique_ptr<Upstream::ProdClusterManagerFactory> cluster_manager_factory_;
  NiceMock<Server::MockListenerComponentFactory> component_factory_;
  NiceMock<Server::MockWorkerFactory> worker_factory_;
  Server::ListenerManagerImpl listener_manager_{server_, component_factory_, worker_factory_};
  Runtime::RandomGeneratorImpl random_;
};

uint32_t run(const std::string& directory) {
  uint32_t num_tested = 0;
  for (const std::string& filename : TestUtility::listFiles(directory, true)) {
    printf("NM: loading config file %s\n", filename.c_str());
    ConfigTest config(filename);
    num_tested++;
  }
  return num_tested;
}

} // namespace ConfigTest
} // namespace Envoy
