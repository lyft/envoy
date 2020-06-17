#pragma once

#include <chrono>

#include "envoy/grpc/async_client.h"

#include "common/config/version_converter.h"

namespace Envoy {
namespace Grpc {
namespace Internal {

/**
 * Forward declarations for helper functions.
 */
void sendMessageUntyped(RawAsyncStream* stream, const Protobuf::Message& request, bool end_stream);
ProtobufTypes::MessagePtr parseMessageUntyped(ProtobufTypes::MessagePtr&& message,
                                              Buffer::InstancePtr&& response);
RawAsyncStream* startUntyped(RawAsyncClient* client,
                             const Protobuf::MethodDescriptor& service_method,
                             RawAsyncStreamCallbacks& callbacks,
                             const Http::AsyncClient::StreamOptions& options);
AsyncRequest* sendUntyped(RawAsyncClient* client, const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message& request, RawAsyncRequestCallbacks& callbacks,
                          Tracing::Span& parent_span,
                          const Http::AsyncClient::RequestOptions& options);

} // namespace Internal

/**
 * Convenience wrapper for an AsyncStream* providing typed protobuf support.
 */
template <typename Request> class AsyncStream /* : public RawAsyncStream */ {
public:
  AsyncStream() = default;
  AsyncStream(RawAsyncStream* stream) : stream_(stream) {}
  AsyncStream(const AsyncStream& other) = default;
  void sendMessage(const Protobuf::Message& request, bool end_stream) {
    Internal::sendMessageUntyped(stream_, std::move(request), end_stream);
  }
  void sendMessage(const Protobuf::Message& request,
                   envoy::config::core::v3::ApiVersion transport_api_version, bool end_stream) {
    Config::VersionConverter::prepareMessageForGrpcWire(const_cast<Protobuf::Message&>(request),
                                                        transport_api_version);
    Internal::sendMessageUntyped(stream_, std::move(request), end_stream);
  }
  void closeStream() { stream_->closeStream(); }
  void resetStream() { stream_->resetStream(); }
  bool isAboveWriteBufferHighWatermark() const {
    return stream_->isAboveWriteBufferHighWatermark();
  }
  AsyncStream* operator->() { return this; }
  AsyncStream<Request> operator=(RawAsyncStream* stream) {
    stream_ = stream;
    return *this;
  }
  bool operator==(RawAsyncStream* stream) const { return stream_ == stream; }
  bool operator!=(RawAsyncStream* stream) const { return stream_ != stream; }

private:
  RawAsyncStream* stream_{};
};

/**
 * Convenience subclasses for AsyncRequestCallbacks.
 */
template <typename Response> class AsyncRequestCallbacks : public RawAsyncRequestCallbacks {
public:
  ~AsyncRequestCallbacks() override = default;
  virtual void onSuccess(std::unique_ptr<Response>&& response, Tracing::Span& span) PURE;

private:
  void onSuccessRaw(Buffer::InstancePtr&& response, Tracing::Span& span) override {
    auto message = std::unique_ptr<Response>(dynamic_cast<Response*>(
        Internal::parseMessageUntyped(std::make_unique<Response>(), std::move(response))
            .release()));
    if (!message) {
      onFailure(Status::WellKnownGrpcStatus::Internal, "", span);
      return;
    }
    onSuccess(std::move(message), span);
  }
};

/**
 * A versioned gRPC client.
 */
class VersionedClient {
public:
  virtual ~VersionedClient() = default;

  /**
   * @return std::string template of a fully-qualified service method name. For example:
   *                     envoy.service.auth.{}.Authorization.Check.
   */
  virtual const std::string methodNameTemplate() const PURE;

  /**
   * Given a version, return the method descriptor for a specific version.
   *
   * @param api_version target API version.
   * @param use_alpha if this is an alpha version of an API client.
   *
   * @return Protobuf::MethodDescriptor of a method for a specific version.
   */
  const Protobuf::MethodDescriptor&
  getMethodDescriptorForVersion(envoy::config::core::v3::ApiVersion api_version,
                                bool use_alpha = false);
};

/**
 * Convenience subclasses for AsyncStreamCallbacks.
 */
template <typename Response> class AsyncStreamCallbacks : public RawAsyncStreamCallbacks {
public:
  ~AsyncStreamCallbacks() override = default;
  virtual void onReceiveMessage(std::unique_ptr<Response>&& message) PURE;

private:
  bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
    auto message = std::unique_ptr<Response>(dynamic_cast<Response*>(
        Internal::parseMessageUntyped(std::make_unique<Response>(), std::move(response))
            .release()));
    if (!message) {
      return false;
    }
    onReceiveMessage(std::move(message));
    return true;
  }
};

template <typename Request, typename Response> class AsyncClient /* : public RawAsyncClient )*/ {
public:
  AsyncClient() = default;
  AsyncClient(RawAsyncClientPtr&& client) : client_(std::move(client)) {}
  virtual ~AsyncClient() = default;

  virtual AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                             const Protobuf::Message& request,
                             AsyncRequestCallbacks<Response>& callbacks, Tracing::Span& parent_span,
                             const Http::AsyncClient::RequestOptions& options) {
    return Internal::sendUntyped(client_.get(), service_method, request, callbacks, parent_span,
                                 options);
  }
  virtual AsyncRequest* send(const Protobuf::MethodDescriptor& service_method,
                             const Protobuf::Message& request,
                             AsyncRequestCallbacks<Response>& callbacks, Tracing::Span& parent_span,
                             const Http::AsyncClient::RequestOptions& options,
                             envoy::config::core::v3::ApiVersion transport_api_version) {
    Config::VersionConverter::prepareMessageForGrpcWire(const_cast<Protobuf::Message&>(request),
                                                        transport_api_version);
    return Internal::sendUntyped(client_.get(), service_method, request, callbacks, parent_span,
                                 options);
  }

  virtual AsyncStream<Request> start(const Protobuf::MethodDescriptor& service_method,
                                     AsyncStreamCallbacks<Response>& callbacks,
                                     const Http::AsyncClient::StreamOptions& options) {
    return AsyncStream<Request>(
        Internal::startUntyped(client_.get(), service_method, callbacks, options));
  }

  AsyncClient* operator->() { return this; }
  void operator=(RawAsyncClientPtr&& client) { client_ = std::move(client); }
  void reset() { client_.reset(); }

private:
  RawAsyncClientPtr client_{};
};

} // namespace Grpc
} // namespace Envoy
