#pragma once

#include "envoy/http/codec.h"

#include "source/common/common/assert.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Http {

class StreamCallbackHelper {
public:
  void runLowWatermarkCallbacks() {
    if (reset_callbacks_started_ || local_end_stream_) {
      return;
    }
    ASSERT(high_watermark_callbacks_ > 0);
    --high_watermark_callbacks_;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onBelowWriteBufferLowWatermark();
      }
    }
  }

  void runHighWatermarkCallbacks() {
    if (reset_callbacks_started_ || local_end_stream_) {
      return;
    }
    ++high_watermark_callbacks_;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onAboveWriteBufferHighWatermark();
      }
    }
  }

  void runResetCallbacks(StreamResetReason reason) {
    // Reset callbacks are a special case, and the only StreamCallbacks allowed
    // to run after local_end_stream_.
    if (reset_callbacks_started_) {
      return;
    }

    reset_callbacks_started_ = true;
    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onResetStream(reason, absl::string_view());
      }
    }
  }

  bool local_end_stream_{};

protected:
  void addCallbacksHelper(StreamCallbacks& callbacks) {
    ASSERT(!reset_callbacks_started_ && !local_end_stream_);
    callbacks_.push_back(&callbacks);
    for (uint32_t i = 0; i < high_watermark_callbacks_; ++i) {
      callbacks.onAboveWriteBufferHighWatermark();
    }
  }

  void removeCallbacksHelper(StreamCallbacks& callbacks) {
    // For performance reasons we just clear the callback and do not resize the vector.
    // Reset callbacks scale with the number of filters per request and do not get added and
    // removed multiple times.
    // The vector may not be safely resized without making sure the run.*Callbacks() helper
    // functions above still handle removeCallbacksHelper() calls mid-loop.
    for (auto& callback : callbacks_) {
      if (callback == &callbacks) {
        callback = nullptr;
        return;
      }
    }
  }

private:
  absl::InlinedVector<StreamCallbacks*, 8> callbacks_;
  bool reset_callbacks_started_{};
  uint32_t high_watermark_callbacks_{};
};

} // namespace Http
} // namespace Envoy
