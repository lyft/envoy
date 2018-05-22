#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::WhenDynamicCastTo;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class MockRequestCallbacks : public RequestCallbacks {
public:
  void onComplete(ResponsePtr&& response) override { onComplete_(response); }

  MOCK_METHOD1(onComplete_, void(ResponsePtr& response));
};

MATCHER_P(AuthzDeniedResponse, response, "") {
  if (arg->status_code != response.status_code) {
    return false;
  }
  if (arg->body.compare(response.body)) {
    return false;
  }
  if (!arg->headers.empty() && response.headers.empty()) {
    return false;
  }
  if (!std::equal(arg->headers.begin(), arg->headers.end(), response.headers.begin())) {
    return false;
  }

  return true;
}

MATCHER_P(AuthzOkResponse, response, "") {
  if (arg->status != response.status) {
    return false;
  }
  if (!arg->headers.empty() && response.headers.empty()) {
    return false;
  }
  if (!std::equal(arg->headers.begin(), arg->headers.end(), response.headers.begin())) {
    return false;
  }

  return true;
}

typedef std::vector<std::pair<std::string, std::string>> StringPairVector;

class ExtAuthzGrpcClientTest : public testing::Test {
public:
  ExtAuthzGrpcClientTest()
      : async_client_(new Grpc::MockAsyncClient()),
        client_(Grpc::AsyncClientPtr{async_client_}, absl::optional<std::chrono::milliseconds>()) {}
  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImpl client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;

  std::unique_ptr<envoy::service::auth::v2alpha::CheckResponse>
  initCheckResponse(Grpc::Status::GrpcStatus response_status = Grpc::Status::GrpcStatus::Ok,
                    envoy::type::StatusCode http_status_code = envoy::type::StatusCode::OK,
                    const std::string& body = std::string{},
                    const StringPairVector& headers = StringPairVector{}) {
    auto response = std::make_unique<envoy::service::auth::v2alpha::CheckResponse>();
    auto status = response->mutable_status();
    status->set_code(response_status);

    if (response_status != Grpc::Status::GrpcStatus::Ok) {
      auto denied_response = response->mutable_denied_response();
      if (!body.empty()) {
        denied_response->set_body(body);
      }

      auto status_code = denied_response->mutable_status();
      status_code->set_code(http_status_code);

      auto denied_response_headers = denied_response->mutable_headers();
      if (!headers.empty()) {
        for (auto& header : headers) {
          denied_response_headers->insert(
              Envoy::Protobuf::MapPair<Envoy::ProtobufTypes::String, Envoy::ProtobufTypes::String>(
                  header.first, header.second));
        }
      }
    } else {
      auto response_headers = response->mutable_ok_response()->mutable_headers();
      ;
      if (!headers.empty()) {
        for (auto& header : headers) {
          response_headers->insert(
              Envoy::Protobuf::MapPair<Envoy::ProtobufTypes::String, Envoy::ProtobufTypes::String>(
                  header.first, header.second));
        }
      }
    }

    return response;
  }

  Response initAuthzResponse(CheckStatus status, Http::Code status_code = Http::Code::OK,
                             const std::string& body = std::string{},
                             const StringPairVector& headers = StringPairVector{}) {
    auto authz_response = Response{};
    authz_response.status = status;
    authz_response.status_code = status_code;
    if (!body.empty()) {
      authz_response.body = body;
    }
    if (!headers.empty()) {
      for (auto& header : headers) {
        authz_response.headers.push_back({Http::LowerCaseString{header.first}, header.second});
      }
    }
    return authz_response;
  }

  void expectCallSend(envoy::service::auth::v2alpha::CheckRequest& request) {
    EXPECT_CALL(*async_client_, send(_, ProtoEq(request), Ref(client_), _, _))
        .WillOnce(
            Invoke([this](const Protobuf::MethodDescriptor& service_method,
                          const Protobuf::Message&, Grpc::AsyncRequestCallbacks&, Tracing::Span&,
                          const absl::optional<std::chrono::milliseconds>&) -> Grpc::AsyncRequest* {
              // TODO(dio): Use a defined constant value.
              EXPECT_EQ("envoy.service.auth.v2alpha.Authorization",
                        service_method.service()->full_name());
              EXPECT_EQ("Check", service_method.name());
              return &async_request_;
            }));
  }
};

TEST_F(ExtAuthzGrpcClientTest, BasicOK) {
  auto check_response = initCheckResponse(Grpc::Status::GrpcStatus::Ok);
  auto authz_response = initAuthzResponse(CheckStatus::OK);

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_ok"));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_.onSuccess(std::move(check_response), span_);
}

TEST_F(ExtAuthzGrpcClientTest, BasicDenied) {
  auto check_response = initCheckResponse(Grpc::Status::GrpcStatus::PermissionDenied,
                                          envoy::type::StatusCode::Forbidden);
  auto authz_response = initAuthzResponse(CheckStatus::Denied, Http::Code::Forbidden);

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_unauthorized"));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(std::move(check_response), span_);
}

TEST_F(ExtAuthzGrpcClientTest, AuthorizationDeniedWithAllAttributes) {
  auto expected_body = std::string{"test"};
  auto expected_headers = StringPairVector{{std::string{"foo"}, std::string{"bar"}}};
  auto check_response =
      initCheckResponse(Grpc::Status::GrpcStatus::PermissionDenied,
                        envoy::type::StatusCode::Unauthorized, expected_body, expected_headers);
  auto authz_response = initAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized,
                                          expected_body, expected_headers);

  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  Http::HeaderMapImpl headers;
  client_.onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag("ext_authz_status", "ext_authz_unauthorized"));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_.onSuccess(std::move(check_response), span_);
}

TEST_F(ExtAuthzGrpcClientTest, BasicError) {
  envoy::service::auth::v2alpha::CheckRequest request;
  expectCallSend(request);
  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  auto authz_response = Response{};
  authz_response.status = CheckStatus::Error;
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));
  client_.onFailure(Grpc::Status::Unknown, "", span_);
}

TEST_F(ExtAuthzGrpcClientTest, Cancel) {
  envoy::service::auth::v2alpha::CheckRequest request;

  EXPECT_CALL(*async_client_, send(_, _, _, _, _)).WillOnce(Return(&async_request_));

  client_.check(request_callbacks_, request, Tracing::NullSpan::instance());

  EXPECT_CALL(async_request_, cancel());
  client_.cancel();
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
