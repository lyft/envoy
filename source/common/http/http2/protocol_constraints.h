#pragma once

#include <cstdint>
#include <functional>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/network/connection.h"

#include "common/http/http2/codec_stats.h"
#include "common/http/status.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Class for detecting abusive peers and validating additional constraints imposed by Envoy.
 * This class does not check protocol compliance with the H/2 standard, as this is checked by
 * protocol framer/codec. Currently implemented constraints:
 * 1. detection of control frame (i.e. PING) initiated floods.
 * 2. detection of outbound DATA or HEADER frame floods.
 * 4. zero length, PRIORITY and WINDOW_UPDATE floods.
 */

class ProtocolConstraints {
public:
  using ReleasorProc = std::function<void()>;

  explicit ProtocolConstraints(CodecStats& stats,
                               const envoy::config::core::v3::Http2ProtocolOptions& http2_options);

  ReleasorProc incrementOutboundFrameCount(bool is_outbound_flood_monitored_control_frame);
  Status status() { return status_; }

  Status trackInboundFrames(const nghttp2_frame_hd* hd, uint32_t padding_length);

  void incrementOutboundDataFrameCount() { ++outbound_data_frames_; }

private:
  void releaseOutboundFrame();
  void releaseOutboundControlFrame();
  Status checkOutboundQueueLimits();
  Status checkInboundFrameLimits();

  Status status_;
  CodecStats& stats_;
  // This counter keeps track of the number of outbound frames of all types (these that were
  // buffered in the underlying connection but not yet written into the socket). If this counter
  // exceeds the `max_outbound_frames_' value the connection is terminated.
  uint32_t outbound_frames_ = 0;
  // Maximum number of outbound frames. Initialized from corresponding http2_protocol_options.
  // Default value is 10000.
  const uint32_t max_outbound_frames_;
  ReleasorProc frame_buffer_releasor_;

  // This counter keeps track of the number of outbound frames of types PING, SETTINGS and
  // RST_STREAM (these that were buffered in the underlying connection but not yet written into the
  // socket). If this counter exceeds the `max_outbound_control_frames_' value the connection is
  // terminated.
  uint32_t outbound_control_frames_ = 0;
  // Maximum number of outbound frames of types PING, SETTINGS and RST_STREAM. Initialized from
  // corresponding http2_protocol_options. Default value is 1000.
  const uint32_t max_outbound_control_frames_;
  ReleasorProc control_frame_buffer_releasor_;

  // This counter keeps track of the number of consecutive inbound frames of types HEADERS,
  // CONTINUATION and DATA with an empty payload and no end stream flag. If this counter exceeds
  // the `max_consecutive_inbound_frames_with_empty_payload_` value the connection is terminated.
  uint32_t consecutive_inbound_frames_with_empty_payload_ = 0;
  // Maximum number of consecutive inbound frames of types HEADERS, CONTINUATION and DATA without
  // a payload. Initialized from corresponding http2_protocol_options. Default value is 1.
  const uint32_t max_consecutive_inbound_frames_with_empty_payload_;

  // This counter keeps track of the number of inbound streams.
  uint32_t inbound_streams_ = 0;
  // This counter keeps track of the number of inbound PRIORITY frames. If this counter exceeds
  // the value calculated using this formula:
  //
  //     max_inbound_priority_frames_per_stream_ * (1 + inbound_streams_)
  //
  // the connection is terminated.
  uint64_t inbound_priority_frames_ = 0;
  // Maximum number of inbound PRIORITY frames per stream. Initialized from corresponding
  // http2_protocol_options. Default value is 100.
  const uint32_t max_inbound_priority_frames_per_stream_;

  // This counter keeps track of the number of inbound WINDOW_UPDATE frames. If this counter exceeds
  // the value calculated using this formula:
  //
  //     1 + 2 * (inbound_streams_ +
  //              max_inbound_window_update_frames_per_data_frame_sent_ * outbound_data_frames_)
  //
  // the connection is terminated.
  uint64_t inbound_window_update_frames_ = 0;
  // This counter keeps track of the number of outbound DATA frames.
  uint64_t outbound_data_frames_ = 0;
  // Maximum number of inbound WINDOW_UPDATE frames per outbound DATA frame sent. Initialized
  // from corresponding http2_protocol_options. Default value is 10.
  const uint32_t max_inbound_window_update_frames_per_data_frame_sent_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
