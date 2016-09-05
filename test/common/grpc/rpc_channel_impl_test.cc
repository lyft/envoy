#include "common/grpc/rpc_channel_impl.h"

#include "test/generated/helloworld.pb.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Grpc {

class GrpcRequestImplTest : public testing::Test {
public:
  GrpcRequestImplTest() : http_async_client_request_(&cm_.async_client_) {
    ON_CALL(cm_.cluster_, features()).WillByDefault(Return(Upstream::Cluster::Features::HTTP2));
  }

  void expectNormalRequest(
      const Optional<std::chrono::milliseconds> timeout = Optional<std::chrono::milliseconds>()) {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_, send_(_, _, timeout))
        .WillOnce(Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                             Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
          http_request_ = std::move(request);
          http_callbacks_ = &callbacks;
          return &http_async_client_request_;
        }));
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  MockRpcChannelCallbacks grpc_callbacks_;
  RpcChannelImpl grpc_request_{cm_, "cluster", grpc_callbacks_, cm_.cluster_.stats_store_,
                               Optional<std::chrono::milliseconds>()};
  helloworld::Greeter::Stub service_{&grpc_request_};
  Http::MockAsyncClientRequest http_async_client_request_;
  Http::MessagePtr http_request_;
  Http::AsyncClient::Callbacks* http_callbacks_{};
};

TEST_F(GrpcRequestImplTest, NoError) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_))
      .WillOnce(Invoke([](Http::HeaderMap& headers) -> void { headers.addViaCopy("foo", "bar"); }));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::HeaderMapImpl expected_request_headers{{":scheme", "http"},
                                               {":method", "POST"},
                                               {":path", "/helloworld.Greeter/SayHello"},
                                               {":authority", "cluster"},
                                               {"content-type", "application/grpc"},
                                               {"foo", "bar"}};

  EXPECT_THAT(http_request_->headers(), HeaderMapEqualRef(expected_request_headers));

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  helloworld::HelloReply inner_response;
  inner_response.set_message("hello a name");
  response_http_message->body(RpcChannelImpl::serializeBody(inner_response));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onSuccess());
  http_callbacks_->onSuccess(std::move(response_http_message));
  EXPECT_EQ(response.SerializeAsString(), inner_response.SerializeAsString());
  EXPECT_EQ(
      1UL,
      cm_.cluster_.stats_store_.counter("cluster.cluster.grpc.helloworld.Greeter.SayHello.success")
          .value());
}

TEST_F(GrpcRequestImplTest, Non200Response) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "503"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "non-200 response code"));
  http_callbacks_->onSuccess(std::move(response_http_message));
  EXPECT_EQ(
      1UL,
      cm_.cluster_.stats_store_.counter("cluster.cluster.grpc.helloworld.Greeter.SayHello.failure")
          .value());
}

TEST_F(GrpcRequestImplTest, NoResponseTrailers) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "no response trailers"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadGrpcStatusInHeaderOnlyResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}, {"grpc-status", "foo"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad grpc-status header"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, HeaderOnlyFailure) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(
      new Http::ResponseMessageImpl(Http::HeaderMapPtr{new Http::HeaderMapImpl{
          {":status", "200"}, {"grpc-status", "3"}, {"grpc-message", "hello"}}}));

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(3), "hello"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadGrpcStatusInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  response_http_message->trailers(Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", ""}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad grpc-status trailer"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, GrpcStatusNonZeroInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "1"}, {"grpc-message", "hello"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(1), "hello"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, ShortBodyInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  response_http_message->body(Buffer::InstancePtr{new Buffer::OwnedImpl("aaa")});
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad serialized body"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, BadMessageInResponse) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  response_http_message->body(Buffer::InstancePtr{new Buffer::OwnedImpl("aaaaaaaa")});
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "bad serialized body"));
  http_callbacks_->onSuccess(std::move(response_http_message));
}

TEST_F(GrpcRequestImplTest, HttpAsyncRequestFailure) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "stream reset"));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
}

TEST_F(GrpcRequestImplTest, HttpAsyncRequestTimeout) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "request timeout"));
  http_callbacks_->onFailure(Http::AsyncClient::FailureReason::RequestTimeout);
}

TEST_F(GrpcRequestImplTest, NoHttpAsyncRequest) {
  EXPECT_CALL(cm_, httpAsyncClientForCluster("cluster")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks.onFailure(Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));
  EXPECT_CALL(grpc_callbacks_, onFailure(Optional<uint64_t>(), "stream reset"));

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);
}

TEST_F(GrpcRequestImplTest, Cancel) {
  expectNormalRequest();

  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_.SayHello(nullptr, &request, &response, nullptr);

  EXPECT_CALL(http_async_client_request_, cancel());
  grpc_request_.cancel();
}

TEST_F(GrpcRequestImplTest, RequestTimeoutSet) {
  const Optional<std::chrono::milliseconds> timeout(std::chrono::milliseconds(100));
  RpcChannelImpl grpc_request_timeout{cm_, "cluster", grpc_callbacks_, cm_.cluster_.stats_store_,
                                      timeout};
  helloworld::Greeter::Stub service_timeout{&grpc_request_timeout};
  expectNormalRequest(timeout);
  helloworld::HelloRequest request;
  request.set_name("a name");
  helloworld::HelloReply response;
  EXPECT_CALL(grpc_callbacks_, onPreRequestCustomizeHeaders(_));
  service_timeout.SayHello(nullptr, &request, &response, nullptr);

  Http::MessagePtr response_http_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{":status", "200"}}}));
  helloworld::HelloReply inner_response;
  inner_response.set_message("hello a name");

  response_http_message->body(RpcChannelImpl::serializeBody(inner_response));
  response_http_message->trailers(
      Http::HeaderMapPtr{new Http::HeaderMapImpl{{"grpc-status", "0"}}});

  EXPECT_CALL(grpc_callbacks_, onSuccess());
  http_callbacks_->onSuccess(std::move(response_http_message));
}

} // Grpc
