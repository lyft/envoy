#include "test/integration/ssl_utility.h"

#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/server.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace Envoy {
namespace Ssl {

ClientContextPtr createClientSslContext(bool alpn, bool san, ContextManager* context_manager) {
  std::string json_plain = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
}
)EOF";

  std::string json_alpn = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1"
}
)EOF";

  std::string json_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "istio:account_a.namespace_foo.cluster.local" ]
}
)EOF";

  std::string json_alpn_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1",
  "verify_subject_alt_name": [ "istio:account_a.namespace_foo.cluster.local" ]
}
)EOF";

  std::string target;
  if (alpn) {
    target = san ? json_alpn_san : json_alpn;
  } else {
    target = san ? json_san : json_plain;
  }
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(target);
  ContextConfigImpl cfg(*loader);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return context_manager->createSslClientContext(*client_stats_store, cfg);
}

Network::Address::InstanceConstSharedPtr getSslAddress(Network::Address::IpVersion version,
                                                       int port) {
  std::string url =
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version) + ":" + std::to_string(port);
  return Network::Utility::resolveUrl(url);
}

} // Ssl
} // Envoy
