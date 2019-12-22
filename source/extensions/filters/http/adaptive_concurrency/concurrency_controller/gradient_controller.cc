#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"

#include <atomic>
#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency/v3alpha/adaptive_concurrency.pb.h"
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
    const envoy::config::filter::http::adaptive_concurrency::v3alpha::GradientControllerConfig&
        proto_config,
    Runtime::Loader& runtime)
    : runtime_(runtime),
      min_rtt_calc_interval_(std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(proto_config.min_rtt_calc_params().interval()))),
      sample_rtt_calc_interval_(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
          proto_config.concurrency_limit_params().concurrency_update_interval()))),
      jitter_pct_(
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config.min_rtt_calc_params(), jitter, 15)),
      max_concurrency_limit_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.concurrency_limit_params(), max_concurrency_limit, 1000)),
      min_rtt_aggregate_request_count_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.min_rtt_calc_params(), request_count, 50)),
      sample_aggregate_percentile_(
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, sample_aggregate_percentile, 50)),
      min_concurrency_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.min_rtt_calc_params(), min_concurrency, 3)),
      min_rtt_buffer_pct_(
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config.min_rtt_calc_params(), buffer, 25)) {}
GradientController::GradientController(GradientControllerConfig config,
                                       Event::Dispatcher& dispatcher, Runtime::Loader&,
                                       const std::string& stats_prefix, Stats::Scope& scope,
                                       Runtime::RandomGenerator& random)
    : config_(std::move(config)), dispatcher_(dispatcher), scope_(scope),
      stats_(generateStats(scope_, stats_prefix)), random_(random), deferred_limit_value_(1),
      num_rq_outstanding_(0), concurrency_limit_(config_.minConcurrency()),
      latency_sample_hist_(hist_fast_alloc(), hist_free) {
  min_rtt_calc_timer_ = dispatcher_.createTimer([this]() -> void { enterMinRTTSamplingWindow(); });

  sample_reset_timer_ = dispatcher_.createTimer([this]() -> void {
    if (inMinRTTSamplingWindow()) {
      // The minRTT sampling window started since the sample reset timer was enabled last. Since the
      // minRTT value is being calculated, let's give up on this timer to avoid blocking the
      // dispatcher thread and rely on it being enabled again as part of the minRTT calculation.
      return;
    }

    {
      absl::MutexLock ml(&sample_mutation_mtx_);
      resetSampleWindow();
    }

    sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval());
  });

  enterMinRTTSamplingWindow();
  sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval());
  stats_.concurrency_limit_.set(concurrency_limit_.load());
}

GradientControllerStats GradientController::generateStats(Stats::Scope& scope,
                                                          const std::string& stats_prefix) {
  return {ALL_GRADIENT_CONTROLLER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix),
                                        POOL_GAUGE_PREFIX(scope, stats_prefix))};
}

void GradientController::enterMinRTTSamplingWindow() {
  absl::MutexLock ml(&sample_mutation_mtx_);

  stats_.min_rtt_calculation_active_.set(config_.minConcurrency());

  // Set the minRTT flag to indicate we're gathering samples to update the value. This will
  // prevent the sample window from resetting until enough requests are gathered to complete the
  // recalculation.
  deferred_limit_value_.store(concurrencyLimit());
  updateConcurrencyLimit(config_.minConcurrency());

  // Throw away any latency samples from before the recalculation window as it may not represent
  // the minRTT.
  hist_clear(latency_sample_hist_.get());
}

void GradientController::updateMinRTT() {
  ASSERT(inMinRTTSamplingWindow());

  {
    absl::MutexLock ml(&sample_mutation_mtx_);
    min_rtt_ = processLatencySamplesAndClear();
    stats_.min_rtt_msecs_.set(
        std::chrono::duration_cast<std::chrono::milliseconds>(min_rtt_).count());
    updateConcurrencyLimit(deferred_limit_value_.load());
    deferred_limit_value_.store(0);
    stats_.min_rtt_calculation_active_.set(0);
  }

  min_rtt_calc_timer_->enableTimer(
      applyJitter(config_.minRTTCalcInterval(), config_.jitterPercent()));
  sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval());
}

std::chrono::milliseconds GradientController::applyJitter(std::chrono::milliseconds interval,
                                                          double jitter_pct) const {
  if (jitter_pct == 0) {
    return interval;
  }

  const uint32_t jitter_range_ms = interval.count() * jitter_pct;
  return std::chrono::milliseconds(interval.count() + (random_.random() % jitter_range_ms));
}

void GradientController::resetSampleWindow() {
  // The sampling window must not be reset while sampling for the new minRTT value.
  ASSERT(!inMinRTTSamplingWindow());

  if (hist_sample_count(latency_sample_hist_.get()) == 0) {
    return;
  }

  sample_rtt_ = processLatencySamplesAndClear();
  stats_.sample_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(sample_rtt_).count());
  updateConcurrencyLimit(calculateNewLimit());
}

std::chrono::microseconds GradientController::processLatencySamplesAndClear() {
  const std::array<double, 1> quantile{config_.sampleAggregatePercentile()};
  std::array<double, 1> calculated_quantile;
  hist_approx_quantile(latency_sample_hist_.get(), quantile.data(), 1, calculated_quantile.data());
  hist_clear(latency_sample_hist_.get());
  return std::chrono::microseconds(static_cast<int>(calculated_quantile[0]));
}

uint32_t GradientController::calculateNewLimit() {
  ASSERT(sample_rtt_.count() > 0);

  // Calculate the gradient value, ensuring it's clamped between 0.5 and 2.0.
  // This prevents extreme changes in the concurrency limit between each sample
  // window.
  const auto buffered_min_rtt = min_rtt_.count() + min_rtt_.count() * config_.minRTTBufferPercent();
  const double raw_gradient = static_cast<double>(buffered_min_rtt) / sample_rtt_.count();
  const double gradient = std::max<double>(0.5, std::min<double>(2.0, raw_gradient));
  stats_.gradient_.set(gradient);

  const double limit = concurrencyLimit() * gradient;
  const double burst_headroom = sqrt(limit);
  stats_.burst_queue_size_.set(burst_headroom);

  // The final concurrency value factors in the burst headroom and must be clamped to keep the value
  // in the range [1, configured_max].
  const uint32_t new_limit = limit + burst_headroom;
  return std::max<uint32_t>(1, std::min<uint32_t>(config_.maxConcurrencyLimit(), new_limit));
}

RequestForwardingAction GradientController::forwardingDecision() {
  // Note that a race condition exists here which would allow more outstanding requests than the
  // concurrency limit bounded by the number of worker threads. After loading num_rq_outstanding_
  // and before loading concurrency_limit_, another thread could potentially swoop in and modify
  // num_rq_outstanding_, causing us to move forward with stale values and increment
  // num_rq_outstanding_.
  //
  // TODO (tonya11en): Reconsider using a CAS loop here.
  if (num_rq_outstanding_.load() < concurrencyLimit()) {
    ++num_rq_outstanding_;
    return RequestForwardingAction::Forward;
  }
  stats_.rq_blocked_.inc();
  return RequestForwardingAction::Block;
}

void GradientController::recordLatencySample(std::chrono::nanoseconds rq_latency) {
  const uint32_t latency_usec =
      std::chrono::duration_cast<std::chrono::microseconds>(rq_latency).count();
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;

  uint32_t sample_count;
  {
    absl::MutexLock ml(&sample_mutation_mtx_);
    hist_insert(latency_sample_hist_.get(), latency_usec, 1);
    sample_count = hist_sample_count(latency_sample_hist_.get());
  }

  if (inMinRTTSamplingWindow() && sample_count >= config_.minRTTAggregateRequestCount()) {
    // This sample has pushed the request count over the request count requirement for the minRTT
    // recalculation. It must now be finished.
    updateMinRTT();
  }
}

void GradientController::cancelLatencySample() {
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;
}

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
