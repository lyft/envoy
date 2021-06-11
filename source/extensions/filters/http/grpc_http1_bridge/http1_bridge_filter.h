#pragma once

#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/grpc/context_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {
/**
 * See docs/configuration/http_filters/grpc_http1_bridge_filter.rst
 */
class Http1BridgeFilter : public Http::StreamFilter {
public:
  explicit Http1BridgeFilter(
      const envoy::extensions::filters::http::grpc_http1_bridge::v3::Config& proto_config,
      Grpc::Context& context)
      : proto_config_(proto_config), context_(context) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  bool doStatTracking() const {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.grpc_bridge_stats_disabled")) {
      return false;
    }
    return request_stat_names_.has_value();
  }

private:
  void chargeStat(const Http::ResponseHeaderOrTrailerMap& headers);
  void setupStatTracking(const Http::RequestHeaderMap& headers);
  void doResponseTrailers(const Http::ResponseHeaderOrTrailerMap& trailers);
  void updateGrpcStatusAndMessage(const Http::ResponseHeaderOrTrailerMap& trailers);
  void updateHttpStatusAndContentLength(const Http::ResponseHeaderOrTrailerMap& trailers,
                                        bool enable_http_status_codes_in_trailers_response);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::ResponseHeaderMap* response_headers_{};
  bool do_bridging_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestStatNames> request_stat_names_;
  const envoy::extensions::filters::http::grpc_http1_bridge::v3::Config& proto_config_;
  Grpc::Context& context_;
};

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
