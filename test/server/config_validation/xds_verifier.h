#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "test/server/config_validation/xds_fuzz.pb.h"

#include "absl/container/flat_hash_map.h"
#include "common/common/assert.h"

namespace Envoy {

class XdsVerifier {
public:
  XdsVerifier(test::server::config_validation::Config::SotwOrDelta sotw_or_delta);
  void listenerAdded(envoy::config::listener::v3::Listener listener);
  void listenerUpdated(envoy::config::listener::v3::Listener listener);
  void listenerRemoved(const std::string& name);
  void drainedListener(const std::string& name);

  void routeAdded(envoy::config::route::v3::RouteConfiguration route);
  void routeUpdated(envoy::config::route::v3::RouteConfiguration route);

  enum ListenerState { WARMING, ACTIVE, DRAINING, REMOVED };
  struct ListenerRepresentation {
    envoy::config::listener::v3::Listener listener;
    ListenerState state;
  };

  const std::vector<ListenerRepresentation>& listeners() const { return listeners_; }

  uint32_t numWarming() { return num_warming_; }
  uint32_t numActive() { return num_active_; }
  uint32_t numDraining() { return num_draining_; }

  void dumpState();

private:
  enum SotwOrDelta { SOTW, DELTA };

  std::string getRoute(const envoy::config::listener::v3::Listener& listener);
  bool hasRoute(const envoy::config::listener::v3::Listener& listener);
  bool hasActiveRoute(const envoy::config::listener::v3::Listener& listener);
  void updateSotwListeners();
  void markForRemoval(ListenerRepresentation& rep);
  std::vector<ListenerRepresentation> listeners_;

  // envoy ignores routes that are not referenced by any resources
  // all_routes_ is used for SOTW, as every previous route is sent in each request
  // active_routes_ holds the routes that envoy knows about, i.e. the routes that are/were
  // referenced by a listener
  absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration> all_routes_;
  absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration> active_routes_;

  uint32_t num_warming_;
  uint32_t num_active_;
  uint32_t num_draining_;

  SotwOrDelta sotw_or_delta_;
};

} // namespace Envoy
