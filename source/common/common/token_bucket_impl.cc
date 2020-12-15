#include "common/common/token_bucket_impl.h"

#include <chrono>

namespace Envoy {

TokenBucketImpl::TokenBucketImpl(uint64_t max_tokens, TimeSource& time_source, double fill_rate,
                                 bool allow_multiple_resets)
    : max_tokens_(max_tokens), fill_rate_(std::abs(fill_rate)), tokens_(max_tokens),
      last_fill_(time_source.monotonicTime()), time_source_(time_source) {
  if (!allow_multiple_resets)
    // initialize only when multiple resets are not allowed.
    reset_once_ = absl::optional<bool>(false);
}

uint64_t TokenBucketImpl::consume(uint64_t tokens, bool allow_partial) {
  absl::WriterMutexLock lock(&mutex_);
  if (tokens_ < max_tokens_) {
    const auto time_now = time_source_.monotonicTime();
    tokens_ = std::min((std::chrono::duration<double>(time_now - last_fill_).count() * fill_rate_) +
                           tokens_,
                       max_tokens_);
    last_fill_ = time_now;
  }

  if (allow_partial) {
    tokens = std::min(tokens, static_cast<uint64_t>(std::floor(tokens_)));
  }

  if (tokens_ < tokens) {
    return 0;
  }

  tokens_ -= tokens;
  return tokens;
}

std::chrono::milliseconds TokenBucketImpl::nextTokenAvailable() {
  // If there are tokens available, return immediately.
  absl::ReaderMutexLock lock(&mutex_);
  if (tokens_ >= 1) {
    return std::chrono::milliseconds(0);
  }
  // TODO(ramaraochavali): implement a more precise way that works for very low rate limits.
  return std::chrono::milliseconds(static_cast<uint64_t>(std::ceil((1 / fill_rate_) * 1000)));
}

void TokenBucketImpl::reset(uint64_t num_tokens) {
  ASSERT(num_tokens <= max_tokens_);
  absl::WriterMutexLock lock(&mutex_);
  // Don't reset if reset before and multiple resets aren't allowed.
  if (reset_once_.has_value() && reset_once_.value()) {
    return;
  }
  tokens_ = num_tokens;
  last_fill_ = time_source_.monotonicTime();
  reset_once_ = absl::optional<bool>(true);
}

} // namespace Envoy
