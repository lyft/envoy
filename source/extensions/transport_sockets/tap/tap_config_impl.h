#pragma once

#include "envoy/event/timer.h"

#include "extensions/common/tap/tap_config_base.h"
#include "extensions/transport_sockets/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

class PerSocketTapperImpl : public PerSocketTapper {
public:
  PerSocketTapperImpl(SocketTapConfigSharedPtr config, Extensions::Common::Tap::PerTapSinkHandleManagerPtr&& sink_handle, const Network::Connection& connection);

  // PerSocketTapper
  void closeSocket(Network::ConnectionEvent event) override;
  void onRead(const Buffer::Instance& data, uint32_t bytes_read) override;
  void onWrite(const Buffer::Instance& data, uint32_t bytes_written, bool end_stream) override;

private:
  void initEvent(envoy::data::tap::v2alpha::SocketEvent&);
  void fillConnectionInfo(envoy::data::tap::v2alpha::Connection& connection);
  void makeBufferedTraceIfNeeded() {
    if (buffered_trace_ == nullptr) {
      buffered_trace_ = Extensions::Common::Tap::makeTraceWrapper();
      buffered_trace_->mutable_socket_buffered_trace()->set_trace_id(connection_.id());
    }
  }
  Extensions::Common::Tap::TraceWrapperPtr makeTraceSegment() {
    Extensions::Common::Tap::TraceWrapperPtr trace = Extensions::Common::Tap::makeTraceWrapper();
    trace->mutable_socket_streamed_trace_segment()->set_trace_id(connection_.id());
    return trace;
  }

  SocketTapConfigSharedPtr config_;
  Extensions::Common::Tap::PerTapSinkHandleManagerPtr sink_handle_;
  const Network::Connection& connection_;
  Extensions::Common::Tap::Matcher::MatchStatusVector statuses_;
  // Must be a shared_ptr because of submitTrace().
  Extensions::Common::Tap::TraceWrapperPtr buffered_trace_;
  uint32_t rx_bytes_buffered_{};
  uint32_t tx_bytes_buffered_{};
};

class SocketTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                            public SocketTapConfig,
                            public std::enable_shared_from_this<SocketTapConfigImpl> {
public:
  SocketTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config, Runtime::Loader& loader,
                      Extensions::Common::Tap::Sink* admin_streamer, TimeSource& time_system,

                      Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                      const LocalInfo::LocalInfo& local_info)
      : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config), loader, admin_streamer,
                                                   cluster_manager, scope, local_info),
        time_source_(time_system) {}

  // SocketTapConfig
  PerSocketTapperPtr createPerSocketTapper(const Network::Connection& connection) override {
    auto sink_handle = createPerTapSinkHandleManager(connection.id());
    if (sink_handle) {
      return std::make_unique<PerSocketTapperImpl>(shared_from_this(), std::move(sink_handle), connection);
    }
    return {};
  }
  TimeSource& timeSource() const override { return time_source_; }

private:
  TimeSource& time_source_;

  friend class PerSocketTapperImpl;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
