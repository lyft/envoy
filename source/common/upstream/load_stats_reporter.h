#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"

#include "api/eds.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * All load reporter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_LOAD_REPORTER_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)
// clang-format on

/**
 * Struct definition for all load reporter stats. @see stats_macros.h
 */
struct LoadReporterStats {
  ALL_LOAD_REPORTER_STATS(GENERATE_COUNTER_STRUCT)
};

class LoadStatsReporter : Grpc::TypedAsyncStreamCallbacks<envoy::api::v2::LoadStatsResponse>,
                          Logger::Loggable<Logger::Id::upstream> {
public:
  LoadStatsReporter(const envoy::api::v2::Node& node, ClusterManager& cluster_manager,
                    Stats::Scope& scope, Grpc::AsyncClientPtr async_client,
                    Event::Dispatcher& dispatcher);

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(std::unique_ptr<envoy::api::v2::LoadStatsResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  void setRetryTimer();
  void establishNewStream();
  void sendLoadStatsRequest();
  void handleFailure();
  void startLoadReportPeriod();

  ClusterManager& cm_;
  LoadReporterStats stats_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  envoy::api::v2::LoadStatsRequest request_;
  std::unique_ptr<envoy::api::v2::LoadStatsResponse> message_;
  std::vector<std::string> clusters_;
};

typedef std::unique_ptr<LoadStatsReporter> LoadStatsReporterPtr;

} // namespace Upstream
} // namespace Envoy
