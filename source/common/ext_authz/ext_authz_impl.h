#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/ext_authz/ext_authz.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/singleton/const_singleton.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace ExtAuthz {

typedef Grpc::AsyncClient<envoy::api::v2::auth::CheckRequest,
                          envoy::api::v2::auth::CheckResponse>
    ExtAuthzAsyncClient;
typedef std::unique_ptr<ExtAuthzAsyncClient> ExtAuthzAsyncClientPtr;

typedef Grpc::AsyncRequestCallbacks<envoy::api::v2::auth::CheckResponse> ExtAuthzAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ext_authz_status";
  const std::string TraceUnauthz = "ext_authz_unauthorized";
  const std::string TraceOk = "ext_authz_ok";
};

typedef ConstSingleton<ConstantValues> Constants;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client.
class GrpcClientImpl : public Client, public ExtAuthzAsyncCallbacks {
public:
  GrpcClientImpl(ExtAuthzAsyncClientPtr&& async_client,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, const envoy::api::v2::auth::CheckRequest& request,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::api::v2::auth::CheckResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  ExtAuthzAsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  Optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const std::string& cluster_name,
                  Upstream::ClusterManager& cm);

  // ExtAuthz::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) override;

private:
  const std::string cluster_name_;
  Upstream::ClusterManager& cm_;
};

class NullClientImpl : public Client {
public:
  // ExtAuthz::Client
  void cancel() override {}
  void check(RequestCallbacks& callbacks, const envoy::api::v2::auth::CheckRequest&,
             Tracing::Span&) override {
    callbacks.complete(CheckStatus::OK);
  }
};

class NullFactoryImpl : public ClientFactory {
public:
  // ExtAuthz::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>&) override {
    return ClientPtr{new NullClientImpl()};
  }
};

class CheckRequestGen: public CheckRequestGenIntf {
public:
  CheckRequestGen() {}
  ~CheckRequestGen() {}

  // ExtAuthz::CheckRequestGenIntf
  void createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks, const Envoy::Http::HeaderMap &headers, envoy::api::v2::auth::CheckRequest& request);
  void createTcpCheck(const Network::ReadFilterCallbacks* callbacks, envoy::api::v2::auth::CheckRequest& request);

private:
  ::envoy::api::v2::Address* get_pbuf_address(const Network::Address::InstanceConstSharedPtr&);
  ::envoy::api::v2::auth::AttributeContext_Peer* get_connection_peer(const Network::Connection *, const std::string&, const bool);
  ::envoy::api::v2::auth::AttributeContext_Peer* get_connection_peer(const Network::Connection&, const std::string&, const bool);
  ::envoy::api::v2::auth::AttributeContext_Request* get_http_request(const Envoy::Http::StreamDecoderFilterCallbacks*, const Envoy::Http::HeaderMap &);
  const std::string proto2str(const Envoy::Http::Protocol&);
  static Envoy::Http::HeaderMap::Iterate fill_http_headers(const Envoy::Http::HeaderEntry&, void *);
};

} // namespace ExtAuthz
} // namespace Envoy
