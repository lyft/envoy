#include "common/http/http2/protocol_constraints.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ProtocolConstraints::ProtocolConstraints(
    CodecStats& stats, const envoy::config::core::v3::Http2ProtocolOptions& http2_options)
    : stats_(stats), max_outbound_frames_(http2_options.max_outbound_frames().value()),
      frame_buffer_releasor_([this]() { releaseOutboundFrame(); }),
      max_outbound_control_frames_(http2_options.max_outbound_control_frames().value()),
      control_frame_buffer_releasor_([this]() { releaseOutboundControlFrame(); }),
      max_consecutive_inbound_frames_with_empty_payload_(
          http2_options.max_consecutive_inbound_frames_with_empty_payload().value()),
      max_inbound_priority_frames_per_stream_(
          http2_options.max_inbound_priority_frames_per_stream().value()),
      max_inbound_window_update_frames_per_data_frame_sent_(
          http2_options.max_inbound_window_update_frames_per_data_frame_sent().value()) {}

ProtocolConstraints::ReleasorProc
ProtocolConstraints::incrementOutboundFrameCount(bool is_outbound_flood_monitored_control_frame) {
  ++outbound_frames_;
  if (is_outbound_flood_monitored_control_frame) {
    ++outbound_control_frames_;
  }
  if (status_.ok()) {
    status_ = checkOutboundQueueLimits();
  }
  return is_outbound_flood_monitored_control_frame ? control_frame_buffer_releasor_
                                                   : frame_buffer_releasor_;
}

void ProtocolConstraints::releaseOutboundFrame() {
  ASSERT(outbound_frames_ >= 1);
  --outbound_frames_;
}

void ProtocolConstraints::releaseOutboundControlFrame() {
  ASSERT(outbound_control_frames_ >= 1);
  --outbound_control_frames_;
  releaseOutboundFrame();
}

Status ProtocolConstraints::checkOutboundQueueLimits() {
  if (outbound_frames_ > max_outbound_frames_) {
    stats_.outbound_flood_.inc();
    return bufferFloodError("Too many frames in the outbound queue.");
  }
  if (outbound_control_frames_ > max_outbound_control_frames_) {
    stats_.outbound_control_flood_.inc();
    return bufferFloodError("Too many control frames in the outbound queue.");
  }
  return okStatus();
}

Status ProtocolConstraints::trackInboundFrames(const nghttp2_frame_hd* hd,
                                               uint32_t padding_length) {
  switch (hd->type) {
  case NGHTTP2_HEADERS:
  case NGHTTP2_CONTINUATION:
    // Track new streams.
    if (hd->flags & NGHTTP2_FLAG_END_HEADERS) {
      inbound_streams_++;
    }
    FALLTHRU;
  case NGHTTP2_DATA:
    // Track frames with an empty payload and no end stream flag.
    if (hd->length - padding_length == 0 && !(hd->flags & NGHTTP2_FLAG_END_STREAM)) {
      consecutive_inbound_frames_with_empty_payload_++;
    } else {
      consecutive_inbound_frames_with_empty_payload_ = 0;
    }
    break;
  case NGHTTP2_PRIORITY:
    inbound_priority_frames_++;
    break;
  case NGHTTP2_WINDOW_UPDATE:
    inbound_window_update_frames_++;
    break;
  default:
    break;
  }

  if (status_.ok()) {
    status_ = checkInboundFrameLimits();
  }
  return status_;
}

Status ProtocolConstraints::checkInboundFrameLimits() {
  if (consecutive_inbound_frames_with_empty_payload_ >
      max_consecutive_inbound_frames_with_empty_payload_) {
    stats_.inbound_empty_frames_flood_.inc();
    return inboundFramesWithEmptyPayloadError();
  }

  if (inbound_priority_frames_ >
      static_cast<uint64_t>(max_inbound_priority_frames_per_stream_) * (1 + inbound_streams_)) {
    stats_.inbound_priority_frames_flood_.inc();
    return bufferFloodError("Too many PRIORITY frames");
  }

  if (inbound_window_update_frames_ >
      1 + 2 * (inbound_streams_ +
               max_inbound_window_update_frames_per_data_frame_sent_ * outbound_data_frames_)) {
    stats_.inbound_window_update_frames_flood_.inc();
    return bufferFloodError("Too many WINDOW_UPDATE frames");
  }

  return okStatus();
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
