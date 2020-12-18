#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

using ScopedResume = std::unique_ptr<Cleanup>;
/**
 * All control plane related stats. @see stats_macros.h
 */
#define ALL_CONTROL_PLANE_STATS(COUNTER, GAUGE, TEXT_READOUT)                                      \
  COUNTER(rate_limit_enforced)                                                                     \
  GAUGE(connected_state, NeverImport)                                                              \
  GAUGE(pending_requests, Accumulate)                                                              \
  TEXT_READOUT(identifier)

/**
 * Struct definition for all control plane stats. @see stats_macros.h
 */
struct ControlPlaneStats {
  ALL_CONTROL_PLANE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                          GENERATE_TEXT_READOUT_STRUCT)
};

struct Watch;

/**
 * Manage one or more gRPC subscriptions on a single stream to management server. This can be used
 * for a single xDS API, e.g. EDS, or to combined multiple xDS APIs for ADS.
 */
class GrpcMux {
public:
  virtual ~GrpcMux() = default;

  /**
   * Initiate stream with management server.
   */
  virtual void start() PURE;

  /**
   * Pause discovery requests for a given API type. This is useful when we're processing an update
   * for LDS or CDS and don't want a flood of updates for RDS or EDS respectively. Discovery
   * requests may later be resumed with resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   *
   * @return a ScopedResume object, which when destructed, resumes the paused discovery requests.
   * A discovery request will be sent if one would have been sent during the pause.
   */
  ABSL_MUST_USE_RESULT virtual ScopedResume pause(const std::string& type_url) PURE;

  /**
   * Pause discovery requests for given API types. This is useful when we're processing an update
   * for LDS or CDS and don't want a flood of updates for RDS or EDS respectively. Discovery
   * requests may later be resumed with resume().
   * @param type_urls type URLs corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   *
   * @return a ScopedResume object, which when destructed, resumes the paused discovery requests.
   * A discovery request will be sent if one would have been sent during the pause.
   */
  ABSL_MUST_USE_RESULT virtual ScopedResume pause(const std::vector<std::string> type_urls) PURE;

  /**
   * Start a configuration subscription asynchronously for some API type and resources.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   * @param resources set of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until GrpcMuxWatch is destroyed.
   * @param resource_decoder how incoming opaque resource objects are to be decoded.
   * @param use_namespace_matching if namespace watch should be created. This is used for creating
   * watches on collections of resources; individual members of a collection are identified by the
   * namespace in resource name.
   * @return Watch* an opaque watch token added or updated, to be used in future addOrUpdateWatch
   *                calls.
   */
  virtual Watch* addWatch(const std::string& type_url, const std::set<std::string>& resources,
                          SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
                          std::chrono::milliseconds init_fetch_timeout,
                          const bool use_namespace_matching) PURE;

  // Updates the list of resource names watched by the given watch. If an added name is new across
  // the whole subscription, or if a removed name has no other watch interested in it, then the
  // subscription will enqueue and attempt to send an appropriate discovery request.
  virtual void updateWatch(const std::string& type_url, Watch* watch,
                           const std::set<std::string>& resources,
                           const bool creating_namespace_watch) PURE;

  /**
   * Cleanup of a Watch* added by addOrUpdateWatch(). Receiving a Watch* from addOrUpdateWatch()
   * makes you responsible for eventually invoking this cleanup.
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   * @param watch the watch to be cleaned up.
   */
  virtual void removeWatch(const std::string& type_url, Watch* watch) PURE;

  /**
   * Retrieves the current pause state as set by pause()/resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster
   * @return bool whether the API is paused.
   */
  virtual bool paused(const std::string& type_url) const PURE;

  /**
   * Passes through to all multiplexed SubscriptionStates. To be called when something
   * definitive happens with the initial fetch: either an update is successfully received,
   * or some sort of error happened.*/
  virtual void disableInitFetchTimeoutTimer() PURE;

  virtual void requestOnDemandUpdate(const std::string& type_url,
                                     const std::set<std::string>& for_update) PURE;

  using TypeUrlMap = absl::flat_hash_map<std::string, std::string>;
  static TypeUrlMap& typeUrlMap() { MUTABLE_CONSTRUCT_ON_FIRST_USE(TypeUrlMap, {}); }

  virtual bool isLegacy() const { return false; }
};

using GrpcMuxPtr = std::unique_ptr<GrpcMux>;
using GrpcMuxSharedPtr = std::shared_ptr<GrpcMux>;

template <class ResponseProto> using ResponseProtoPtr = std::unique_ptr<ResponseProto>;
/**
 * A grouping of callbacks that a GrpcMux should provide to its GrpcStream.
 */
template <class ResponseProto> class GrpcStreamCallbacks {
public:
  virtual ~GrpcStreamCallbacks() = default;

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to the
   * gRPC stream having been successfully established.
   */
  virtual void onStreamEstablished() PURE;

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to
   * failure to establish the gRPC stream.
   */
  virtual void onEstablishmentFailure() PURE;

  /**
   * For the GrpcStream to pass received protos to the context.
   */
  virtual void onDiscoveryResponse(ResponseProtoPtr<ResponseProto>&& message,
                                   ControlPlaneStats& control_plane_stats) PURE;

  /**
   * For the GrpcStream to call when its rate limiting logic allows more requests to be sent.
   */
  virtual void onWriteable() PURE;
};

} // namespace Config
} // namespace Envoy
