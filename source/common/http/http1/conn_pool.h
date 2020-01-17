#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/http/codec_wrappers.h"
#include "common/http/conn_pool_base.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * A connection pool implementation for HTTP/1.1 connections.
 * NOTE: The connection pool does NOT do DNS resolution. It assumes it is being given a numeric IP
 *       address. Higher layer code should handle resolving DNS on error and creating a new pool
 *       bound to a different IP address.
 */
class ConnPoolImpl : public ConnPoolImplBase {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority,
               const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  ~ConnPoolImpl() override;

  // ConnectionPool::Instance
  Http::Protocol protocol() const override { return Http::Protocol::Http11; }

  // ConnPoolImplBase
  ActiveClientPtr instantiateActiveClient() override;

protected:
  struct ActiveClient;

  struct StreamWrapper : public StreamEncoderWrapper,
                         public StreamDecoderWrapper,
                         public StreamCallbacks {
    StreamWrapper(StreamDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper() override;

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason, absl::string_view) override {
      parent_.parent().onDownstreamReset(parent_);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ActiveClient& parent_;
    bool encode_complete_{};
    bool close_connection_{};
    bool decode_complete_{};
  };

  using StreamWrapperPtr = std::unique_ptr<StreamWrapper>;

  struct ActiveClient : public ConnPoolImplBase::ActiveClient {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient() override = default;

    ConnPoolImpl& parent() { return static_cast<ConnPoolImpl&>(parent_); }

    // ConnPoolImplBase::ActiveClient
    bool hasActiveRequests() const override;
    bool closingWithIncompleteRequest() const override;
    StreamEncoder& newStreamEncoder(StreamDecoder& response_decoder) override;

    StreamWrapperPtr stream_wrapper_;
  };

  void onDownstreamReset(ActiveClient& client);
  void onResponseComplete(ActiveClient& client);
  ActiveClient& firstReady() const { return static_cast<ActiveClient&>(*ready_clients_.front()); }
  ActiveClient& firstBusy() const { return static_cast<ActiveClient&>(*busy_clients_.front()); }

  Event::TimerPtr upstream_ready_timer_;
  bool upstream_ready_enabled_{false};
};

/**
 * Production implementation of the ConnPoolImpl.
 */
class ProdConnPoolImpl : public ConnPoolImpl {
public:
  ProdConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                   Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
      : ConnPoolImpl(dispatcher, host, priority, options, transport_socket_options) {}

  // ConnPoolImpl
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                 Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

} // namespace Http1
} // namespace Http
} // namespace Envoy
