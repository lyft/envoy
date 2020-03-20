#include "extensions/filters/http/grpc_stats/grpc_stats_filter.h"

#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

namespace {

// A map from gRPC service/method name to symbolized stat names
// for the service/method.
//
// The expected usage pattern is that the map is populated once,
// and can then be queried lock-free as long as it isn't being
// modified.
class GrpcServiceMethodToRequestNamesMap {
public:
  GrpcServiceMethodToRequestNamesMap(Stats::SymbolTable& symbol_table)
      : stat_name_pool_(symbol_table) {}

  void populate(const envoy::config::core::v3::GrpcMethodList& method_list) {
    for (const auto& service : method_list.services()) {
      Stats::StatName stat_name_service = stat_name_pool_.add(service.name());

      StringMap<Grpc::Context::RequestStatNames>& method_map = map_[service.name()];
      for (const auto& method_name : service.method_names()) {
        Stats::StatName stat_name_method = stat_name_pool_.add(method_name);
        method_map[method_name] =
            Grpc::Context::RequestStatNames{stat_name_service, stat_name_method};
      }
    }
  }

  absl::optional<Grpc::Context::RequestStatNames>
  lookup(const Grpc::Common::RequestNames& request_names) const {
    auto service_it = map_.find(request_names.service_);
    if (service_it != map_.end()) {
      const auto& method_map = service_it->second;

      auto method_it = method_map.find(request_names.method_);
      if (method_it != method_map.end()) {
        return method_it->second;
      }
    }

    return {};
  }

private:
  StringMap<StringMap<Grpc::Context::RequestStatNames>> map_;
  Stats::StatNamePool stat_name_pool_;
};

struct Config {
  Config(const envoy::extensions::filters::http::grpc_stats::v3::FilterConfig& proto_config,
         Server::Configuration::FactoryContext& context)
      : context_(context.grpcContext()), emit_filter_state_(proto_config.emit_filter_state()),
        whitelist_(context.scope().symbolTable()) {

    switch (proto_config.per_method_stat_specifier_case()) {
    case envoy::extensions::filters::http::grpc_stats::v3::FilterConfig::
        PER_METHOD_STAT_SPECIFIER_NOT_SET:
      stats_for_all_methods_ = !Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.grpc_stats_filter_disable_stats_for_all_methods_by_default");
      break;

    case envoy::extensions::filters::http::grpc_stats::v3::FilterConfig::kStatsForAllMethods:
      stats_for_all_methods_ = proto_config.stats_for_all_methods();
      break;

    case envoy::extensions::filters::http::grpc_stats::v3::FilterConfig::
        kIndividualMethodStatsWhitelist:
      whitelist_.populate(proto_config.individual_method_stats_whitelist());
      break;
    }
  }
  Grpc::Context& context_;
  bool emit_filter_state_;
  bool stats_for_all_methods_{false};
  GrpcServiceMethodToRequestNamesMap whitelist_;
};
using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class GrpcStatsFilter : public Http::PassThroughFilter {
public:
  GrpcStatsFilter(ConfigConstSharedPtr config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    grpc_request_ = Grpc::Common::hasGrpcContentType(headers);
    if (grpc_request_) {
      cluster_ = decoder_callbacks_->clusterInfo();
      if (cluster_) {
        if (config_->stats_for_all_methods_) {
          // Get dynamically-allocated Context::RequestStatNames from the context.
          request_names_ = config_->context_.resolveServiceAndMethod(headers.Path());
          do_stat_tracking_ = request_names_.has_value();
        } else {
          // This case handles both proto_config.stats_for_all_methods() == false,
          // and proto_config.has_individual_method_stats_whitelist(). This works
          // because proto_config.stats_for_all_methods() == false results in
          // an empty whitelist, which exactly matches the behavior specified for
          // this configuration.
          //
          // Resolve the service and method to a string_view, then get
          // the Context::RequestStatNames out of the pre-allocated list that
          // can be produced with the whitelist being present.
          absl::optional<Grpc::Common::RequestNames> request_names =
              Grpc::Common::resolveServiceAndMethod(headers.Path());

          if (request_names) {
            // Do stat tracking as long as this looks like a grpc service/method,
            // even if it isn't in the whitelist. Things not in the whitelist
            // are counted with a stat with no service/method in the name.
            do_stat_tracking_ = true;

            // If the entry is not found in the whitelist, this will return
            // an empty optional; each of the `charge` functions on the context
            // will interpret an empty optional for this value to mean that the
            // service.method prefix on the stat should be omitted.
            request_names_ = config_->whitelist_.lookup(*request_names);
          }
        }
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    if (grpc_request_) {
      uint64_t delta = request_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          config_->context_.chargeRequestMessageStat(*cluster_, request_names_, delta);
        }
      }
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    grpc_response_ = Grpc::Common::isGrpcResponseHeader(headers, end_stream);
    if (doStatTracking()) {
      config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                   headers.GrpcStatus());
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    if (grpc_response_) {
      uint64_t delta = response_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          config_->context_.chargeResponseMessageStat(*cluster_, request_names_, delta);
        }
      }
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    if (doStatTracking()) {
      config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                   trailers.GrpcStatus());
    }
    return Http::FilterTrailersStatus::Continue;
  }

  bool doStatTracking() const { return do_stat_tracking_; }

  void maybeWriteFilterState() {
    if (!config_->emit_filter_state_) {
      return;
    }
    if (filter_object_ == nullptr) {
      auto state = std::make_unique<GrpcStatsObject>();
      filter_object_ = state.get();
      decoder_callbacks_->streamInfo().filterState()->setData(
          HttpFilterNames::get().GrpcStats, std::move(state),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::FilterChain);
    }
    filter_object_->request_message_count = request_counter_.frameCount();
    filter_object_->response_message_count = response_counter_.frameCount();
  }

private:
  ConfigConstSharedPtr config_;
  GrpcStatsObject* filter_object_{};
  bool do_stat_tracking_{false};
  bool grpc_request_{false};
  bool grpc_response_{false};
  Grpc::FrameInspector request_counter_;
  Grpc::FrameInspector response_counter_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestStatNames> request_names_;
}; // namespace

} // namespace

Http::FilterFactoryCb GrpcStatsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_stats::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, factory_context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStatsFilter>(config));
  };
}

/**
 * Static registration for the gRPC stats filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatsFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
