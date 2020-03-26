#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/codes.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/**
 * All stats for the admission control filter.
 */
#define ALL_ADMISSION_CONTROL_STATS(COUNTER) COUNTER(rq_rejected)

/**
 * Wrapper struct for admission control filter stats. @see stats_macros.h
 */
struct AdmissionControlStats {
  ALL_ADMISSION_CONTROL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Thread-local object to track request counts and successes over a rolling time window. Request
 * data for the time window is kept recent via a circular buffer that phases out old request/success
 * counts when recording new samples.
 *
 * The look-back window for request samples is accurate up to a hard-coded 1-second granularity.
 * TODO (tonya11en): Allow the granularity to be configurable.
 */
class ThreadLocalController {
public:
  virtual ~ThreadLocalController() = default;
  virtual void recordSuccess() PURE;
  virtual void recordFailure() PURE;
  virtual uint32_t requestTotalCount() PURE;
  virtual uint32_t requestSuccessCount() PURE;
};

class ThreadLocalControllerImpl : public ThreadLocalController,
                                  public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalControllerImpl(TimeSource& time_source, std::chrono::seconds sampling_window);
  virtual ~ThreadLocalControllerImpl() = default;
  virtual void recordSuccess() override { recordRequest(true); }
  virtual void recordFailure() override { recordRequest(false); }

  virtual uint32_t requestTotalCount() override {
    maybeUpdateHistoricalData();
    return global_data_.requests;
  }
  virtual uint32_t requestSuccessCount() override {
    maybeUpdateHistoricalData();
    return global_data_.successes;
  }

private:
  struct RequestData {
    RequestData() : requests(0), successes(0) {}
    uint32_t requests;
    uint32_t successes;
  };

  void recordRequest(const bool success);

  // Potentially remove any stale samples and record sample aggregates to the historical data.
  void maybeUpdateHistoricalData();

  // Returns the age of the oldest sample in the historical data.
  std::chrono::microseconds ageOfOldestSample() const {
    ASSERT(!historical_data_.empty());
    using namespace std::chrono;
    return duration_cast<microseconds>(time_source_.monotonicTime() -
                                       historical_data_.front().first);
  }

  // Returns the age of the newest sample in the historical data.
  std::chrono::microseconds ageOfNewestSample() const {
    ASSERT(!historical_data_.empty());
    using namespace std::chrono;
    return duration_cast<microseconds>(time_source_.monotonicTime() -
                                       historical_data_.back().first);
  }

  // Removes the oldest sample in the historical data and reconciles the global data.
  void removeOldestSample() {
    ASSERT(!historical_data_.empty());
    global_data_.successes -= historical_data_.front().second.successes;
    global_data_.requests -= historical_data_.front().second.requests;
    historical_data_.pop_front();
  }

  TimeSource& time_source_;
  std::deque<std::pair<MonotonicTime, RequestData>> historical_data_;

  // Request data aggregated for the whole look-back window.
  RequestData global_data_;

  // The rolling time window size.
  std::chrono::seconds sampling_window_;
};

using AdmissionControlProto =
    envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl;

/**
 * Determines of a request was successful based on response headers.
 */
class ResponseEvaluator {
public:
  virtual ~ResponseEvaluator() = default;
  virtual bool isHttpSuccess(uint64_t code) const PURE;
  virtual bool isGrpcSuccess(Grpc::Status::GrpcStatus status) const PURE;
};

class DefaultResponseEvaluator : public ResponseEvaluator {
public:
  DefaultResponseEvaluator(AdmissionControlProto::DefaultSuccessCriteria success_criteria);
  virtual bool isHttpSuccess(uint64_t code) const override;
  virtual bool isGrpcSuccess(Grpc::Status::GrpcStatus status) const override;

private:
  std::vector<std::function<bool(uint64_t)>> http_success_fns_;
  std::unordered_set<uint64_t> grpc_success_codes_;
};

/**
 * Configuration for the admission control filter.
 */
class AdmissionControlFilterConfig {
public:
  AdmissionControlFilterConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
                               TimeSource& time_source, Runtime::RandomGenerator& random,
                               Stats::Scope& scope, ThreadLocal::SlotPtr&& tls);
  virtual ~AdmissionControlFilterConfig() {}

  virtual ThreadLocalController& getController() const {
    return tls_->getTyped<ThreadLocalControllerImpl>();
  }

  Runtime::Loader& runtime() const { return runtime_; }
  Runtime::RandomGenerator& random() const { return random_; }
  bool filterEnabled() const { return admission_control_feature_.enabled(); }
  TimeSource& timeSource() const { return time_source_; }
  Stats::Scope& scope() const { return scope_; }
  double aggression() const;
  std::shared_ptr<ResponseEvaluator> response_evaluator() const { return response_evaluator_; }

private:
  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  Runtime::RandomGenerator& random_;
  Stats::Scope& scope_;
  const ThreadLocal::SlotPtr tls_;
  Runtime::FeatureFlag admission_control_feature_;
  std::unique_ptr<Runtime::Double> aggression_;
  std::shared_ptr<ResponseEvaluator> response_evaluator_;
};

using AdmissionControlFilterConfigSharedPtr = std::shared_ptr<const AdmissionControlFilterConfig>;

/**
 * A filter that probabilistically rejects requests based on upstream success-rate.
 */
class AdmissionControlFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                         const std::string& stats_prefix);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  static AdmissionControlStats generateStats(Stats::Scope& scope, const std::string& prefix) {
    return {ALL_ADMISSION_CONTROL_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool shouldRejectRequest() const;

  void recordSuccess() {
    config_->getController().recordSuccess();
    ASSERT(deferred_record_failure_);
    deferred_record_failure_->cancel();
  }

  void recordFailure() {
    deferred_record_failure_.reset();
  }

  AdmissionControlFilterConfigSharedPtr config_;
  AdmissionControlStats stats_;
  std::unique_ptr<Cleanup> deferred_record_failure_;
  bool expect_grpc_status_in_trailer_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
