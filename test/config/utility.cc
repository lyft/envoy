#include "test/config/utility.h"

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "api/filter/http_connection_manager.pb.h"

namespace Envoy {

std::string basic_config = R"EOF(
{
  "static_resources": {
    "listeners":[{
      "name":"listener_0",
      "address":{ "socket_address":{ "address":"::1", "port_value":0 } },
      "filter_chains":[{
        "filters":[{
          "name":"http_connection_manager",
          "config": {
            "httpFilters":[ {
              "name":"router",
              "config": { "deprecated_v1":true}
            } ],
            "codecType":"HTTP1",
            "routeConfig": {
              "virtualHosts": [
                {
                  "name":" integration",
                  "routes":[
                  {
                    "route": {"cluster":"cluster_0"},
                    "match": {"prefix":"/"}
                  }],
                  "domains":["*"]}
              ],
              "name":"route_config_0"
            }
          }
        }]
      }]
    }],
    "clusters":[ {
      "name":"cluster_0",
      "connect_timeout":"5s",
      "hosts":[ { "socket_address": {"address":"::1"}}]
    }]
  },
  "admin": {
    "access_log_path":"/dev/null",
    "address": { "socket_address": {"address":"::1", "port_value":0} }
  }
})EOF";

ConfigHelper::ConfigHelper(const Network::Address::IpVersion version) {
  std::string filename =
      TestEnvironment::writeStringToFileForTest("basic_config.json", basic_config);
  MessageUtil::loadFromFile(filename, bootstrap_);

  // Fix up all the socket addresses with the correct version.
  auto* admin = bootstrap_.mutable_admin();
  auto* admin_socket_addr = admin->mutable_address()->mutable_socket_address();
  admin_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* static_resources = bootstrap_.mutable_static_resources();
  auto* listener = static_resources->mutable_listeners(0);
  auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
  listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* cluster = static_resources->mutable_clusters(0);
  auto host_socket_addr = cluster->mutable_hosts(0)->mutable_socket_address();
  host_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
}

void ConfigHelper::setUpstreamPorts(std::vector<uint32_t> ports) {
  uint32_t port_idx = 0;
  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    for (int j = 0; j < cluster->hosts_size(); ++j) {
      auto* host_socket_addr = cluster->mutable_hosts(j)->mutable_socket_address();
      ASSERT(ports.size() >= port_idx);
      host_socket_addr->set_port_value(ports[port_idx++]);
    }
  }
  ASSERT(port_idx == ports.size());
}

void ConfigHelper::setSourceAddress(std::string address_string) {
  bootstrap_.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address(address_string);
}

} // namespace Envoy
