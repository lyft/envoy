#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"

#include <atomic>
#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"

#include "common/common/cleanup.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

GradientControllerConfig::GradientControllerConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig&
        proto_config)
    : min_rtt_calc_interval_(std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(proto_config.min_rtt_calc_params().interval()))),
      sample_rtt_calc_interval_(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
          proto_config.concurrency_limit_params().concurrency_update_interval()))),
      max_concurrency_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.concurrency_limit_params(), max_concurrency_limit, 1000)),
      min_rtt_aggregate_request_count_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.min_rtt_calc_params(), request_count, 50)),
      max_gradient_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.concurrency_limit_params(),
                                                    max_gradient, 2.0)),
      sample_aggregate_percentile_(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                                       proto_config, sample_aggregate_percentile, 1000, 500) /
                                   1000.0) {}

GradientController::GradientController(GradientControllerConfigSharedPtr config,
                                       Event::Dispatcher& dispatcher, Runtime::Loader&,
                                       const std::string& stats_prefix, Stats::Scope& scope)
    : config_(std::move(config)), dispatcher_(dispatcher), scope_(scope),
      stats_(generateStats(scope_, stats_prefix)), recalculating_min_rtt_(true),
      num_rq_outstanding_(0), concurrency_limit_(1),
      latency_sample_hist_(hist_fast_alloc(), hist_free) {
  min_rtt_calc_timer_ = dispatcher_.createTimer([this]() -> void {
    absl::MutexLock ml(&update_window_mtx_);
    setMinRTTSamplingWindow();
  });

  sample_reset_timer_ = dispatcher_.createTimer([this]() -> void {
    {
      absl::MutexLock ml(&update_window_mtx_);
      resetSampleWindow();
    }
    sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  });

  sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  stats_.concurrency_limit_.set(concurrency_limit_.load());
}

GradientControllerStats GradientController::generateStats(Stats::Scope& scope,
                                                          const std::string& stats_prefix) {
  return {ALL_GRADIENT_CONTROLLER_STATS(POOL_GAUGE_PREFIX(scope, stats_prefix))};
}

void GradientController::setMinRTTSamplingWindow() {
  // Set the minRTT flag to indicate we're gathering samples to update the value. This will
  // prevent the sample window from resetting until enough requests are gathered to complete the
  // recalculation.
  concurrency_limit_.store(1);
  stats_.concurrency_limit_.set(concurrency_limit_.load());
  recalculating_min_rtt_.store(true);

  // Throw away any latency samples from before the recalculation window as it may not represent
  // the minRTT.
  absl::MutexLock ml(&latency_sample_mtx_);
  hist_clear(latency_sample_hist_.get());
}

void GradientController::updateMinRTT() {
  ASSERT(recalculating_min_rtt_.load());

  // Reset the timer to ensure the next minRTT sampling window upon leaving scope.
  auto defer =
      Cleanup([this]() { min_rtt_calc_timer_->enableTimer(config_->min_rtt_calc_interval()); });

  absl::MutexLock ml(&latency_sample_mtx_);
  min_rtt_ = processLatencySamplesAndClear();
  stats_.min_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(min_rtt_).count());
  recalculating_min_rtt_.store(false);
}

void GradientController::resetSampleWindow() {
  // The sampling window must not be reset while sampling for the new minRTT value.
  if (recalculating_min_rtt_.load()) {
    return;
  }

  absl::MutexLock ml(&latency_sample_mtx_);
  if (hist_sample_count(latency_sample_hist_.get()) == 0) {
    return;
  }

  sample_rtt_ = processLatencySamplesAndClear();
  concurrency_limit_.store(calculateNewLimit());
  stats_.concurrency_limit_.set(concurrency_limit_.load());
}

std::chrono::microseconds GradientController::processLatencySamplesAndClear() {
  const std::array<double, 1> quantile{config_->sample_aggregate_percentile()};
  std::array<double, 1> calculated_quantile;
  hist_approx_quantile(latency_sample_hist_.get(), quantile.data(), 1, calculated_quantile.data());
  hist_clear(latency_sample_hist_.get());
  return std::chrono::microseconds(static_cast<int>(calculated_quantile[0]));
}

uint32_t GradientController::calculateNewLimit() {
  // Calculate the gradient value, ensuring it remains below the configured maximum.
  ASSERT(sample_rtt_.count() > 0);
  const double raw_gradient = static_cast<double>(min_rtt_.count()) / sample_rtt_.count();
  const double gradient = std::min(config_->max_gradient(), raw_gradient);
  stats_.gradient_.set(gradient);

  const double limit = concurrency_limit_.load() * gradient;
  const double burst_headroom = sqrt(limit);
  stats_.burst_queue_size_.set(burst_headroom);

  // The final concurrency value factors in the burst headroom and must be clamped to keep the value
  // in the range [1, configured_max].
  const auto clamp = [](int min, int max, int val) { return std::max(min, std::min(max, val)); };
  const uint32_t new_limit = limit + burst_headroom;
  return clamp(1, config_->max_concurrency_limit(), new_limit);
}

RequestForwardingAction GradientController::forwardingDecision() {
  // Note that a race condition exists here which would allow more outstanding requests than the
  // concurrency limit bounded by the number of worker threads. After loading num_rq_outstanding_
  // and before loading concurrency_limit_, another thread could potentially swoop in and modify
  // num_rq_outstanding_, causing us to move forward with stale values and increment
  // num_rq_outstanding_.
  //
  // TODO (tonya11en): Reconsider using a CAS loop here.
  if (num_rq_outstanding_.load() < concurrency_limit_.load()) {
    ++num_rq_outstanding_;
    return RequestForwardingAction::Forward;
  }
  return RequestForwardingAction::Block;
}

void GradientController::recordLatencySample(std::chrono::nanoseconds rq_latency) {
  const uint32_t latency_usec =
      std::chrono::duration_cast<std::chrono::microseconds>(rq_latency).count();
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;

  uint32_t sample_count;
  {
    absl::MutexLock ml(&latency_sample_mtx_);
    hist_insert(latency_sample_hist_.get(), latency_usec, 1);
    sample_count = hist_sample_count(latency_sample_hist_.get());
  }

  if (recalculating_min_rtt_.load() && sample_count >= config_->min_rtt_aggregate_request_count()) {
    // This sample has pushed the request count over the request count requirement for the minRTT
    // recalculation. It must now be finished.
    updateMinRTT();
  }
}

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
