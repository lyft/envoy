#include "envoy/service/auth/v2alpha/external_auth.pb.h" // for proto link
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::Values;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

using Params = std::tuple<envoy::config::core::v3::ApiVersion, bool>;

class ExtAuthzGrpcClientTest : public testing::TestWithParam<Params> {
public:
  ExtAuthzGrpcClientTest() : async_client_(new Grpc::MockAsyncClient()), timeout_(10) {}

  void initialize(const Params& param, bool internal_timeout = false) {
    api_version_ = std::get<0>(param);
    use_alpha_ = std::get<1>(param);
    client_ =
        std::make_unique<GrpcClientImpl>(Grpc::RawAsyncClientPtr{async_client_}, internal_timeout,
                                         timeout_, api_version_, use_alpha_);
  }

  void expectCallSend(envoy::service::auth::v3::CheckRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(TestUtility::getVersionedServiceFullName(
                            "envoy.service.auth.{}.Authorization", api_version_, use_alpha_),
                        service_full_name);
              EXPECT_EQ("Check", method_name);
              EXPECT_EQ(timeout_->count(), options.timeout->count());
              return &async_request_;
            }));
  }
  void expectCallSendWithNoTimeout(envoy::service::auth::v3::CheckRequest& request) {
    EXPECT_CALL(*async_client_,
                sendRaw(_, _, Grpc::ProtoBufferEq(request), Ref(*(client_.get())), _, _))
        .WillOnce(
            Invoke([this](absl::string_view service_full_name, absl::string_view method_name,
                          Buffer::InstancePtr&&, Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
              EXPECT_EQ(TestUtility::getVersionedServiceFullName(
                            "envoy.service.auth.{}.Authorization", api_version_, use_alpha_),
                        service_full_name);
              EXPECT_EQ("Check", method_name);
              EXPECT_FALSE(options.timeout.has_value());
              return &async_request_;
            }));
  }

  Grpc::MockAsyncClient* async_client_;
  absl::optional<std::chrono::milliseconds> timeout_;
  Grpc::MockAsyncRequest async_request_;
  GrpcClientImplPtr client_;
  MockRequestCallbacks request_callbacks_;
  Tracing::MockSpan span_;
  bool use_alpha_{};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::ApiVersion api_version_;
};

INSTANTIATE_TEST_SUITE_P(Parameterized, ExtAuthzGrpcClientTest,
                         Values(Params(envoy::config::core::v3::ApiVersion::AUTO, false),
                                Params(envoy::config::core::v3::ApiVersion::V2, false),
                                Params(envoy::config::core::v3::ApiVersion::V2, true),
                                Params(envoy::config::core::v3::ApiVersion::V3, false)));

// Test the client when an ok response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOk) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();

  ProtobufWkt::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["foo"] = ValueUtil::stringValue("ok");
  (*metadata_fields)["bar"] = ValueUtil::numberValue(1);

  // The expected dynamic metadata is set to the outer check response, hence regardless the
  // check_response's http_response value (either OkHttpResponse or DeniedHttpResponse) the dynamic
  // metadata is set to be equal to the check response's dynamic metadata.
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);

  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;

  authz_response.dynamic_metadata = expected_dynamic_metadata;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an ok response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOkWithAllAtributes) {
  initialize(GetParam());

  const std::string empty_body{};
  const auto expected_headers = TestCommon::makeHeaderValueOption({{"foo", "bar", false}});
  auto check_response = TestCommon::makeCheckResponse(
      Grpc::Status::WellKnownGrpcStatus::Ok, envoy::type::v3::OK, empty_body, expected_headers);
  auto authz_response =
      TestCommon::makeAuthzResponse(CheckStatus::OK, Http::Code::OK, empty_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzOkResponse(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDenied) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a gRPC status code unknown is received from the authorization server.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDeniedGrpcUnknownStatus) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();
  status->set_code(Grpc::Status::WellKnownGrpcStatus::Unknown);
  auto authz_response = Response{};
  authz_response.status = CheckStatus::Denied;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when a denied response with additional HTTP attributes is received.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationDeniedWithAllAttributes) {
  initialize(GetParam());

  const std::string expected_body{"test"};
  const auto expected_headers =
      TestCommon::makeHeaderValueOption({{"foo", "bar", false}, {"foobar", "bar", true}});
  auto check_response =
      TestCommon::makeCheckResponse(Grpc::Status::WellKnownGrpcStatus::PermissionDenied,
                                    envoy::type::v3::Unauthorized, expected_body, expected_headers);
  auto authz_response = TestCommon::makeAuthzResponse(CheckStatus::Denied, Http::Code::Unauthorized,
                                                      expected_body, expected_headers);

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);
  EXPECT_EQ(nullptr, headers.RequestId());
  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_unauthorized")));
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzDeniedResponse(authz_response))));

  client_->onSuccess(std::move(check_response), span_);
}

// Test the client when an unknown error occurs.
TEST_P(ExtAuthzGrpcClientTest, UnknownError) {
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::Unknown, "", span_);
}

// Test the client when the request is canceled.
TEST_P(ExtAuthzGrpcClientTest, CancelledAuthorizationRequest) {
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request_));
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when the request times out.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationRequestTimeout) {
  initialize(GetParam());

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  client_->onFailure(Grpc::Status::DeadlineExceeded, "", span_);
}

// Test the client when the request times out on an internal timeout.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationInternalRequestTimeout) {
  initialize(GetParam(), true);

  EXPECT_CALL(*async_client_, dispatcher());
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&async_client_->dispatcher_);
  EXPECT_CALL(*timer, enableTimer(timeout_.value(), _));

  envoy::service::auth::v3::CheckRequest request;
  expectCallSendWithNoTimeout(request);

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(request_callbacks_,
              onComplete_(WhenDynamicCastTo<ResponsePtr&>(AuthzErrorResponse(CheckStatus::Error))));
  timer->invokeCallback();
}

// Test when the client is cancelled with internal timer.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationInternalRequestTimeoutCancelled) {
  initialize(GetParam(), true);

  EXPECT_CALL(*async_client_, dispatcher());
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&async_client_->dispatcher_);
  EXPECT_CALL(*timer, enableTimer(timeout_.value(), _));

  envoy::service::auth::v3::CheckRequest request;
  expectCallSendWithNoTimeout(request);

  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  EXPECT_CALL(async_request_, cancel());
  EXPECT_CALL(request_callbacks_, onComplete_(_)).Times(0);
  client_->cancel();
}

// Test that the internal timer is not used when dispatcher is nullptr.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationRequestInternalTimeoutWithNoDispatcher) {
  initialize(GetParam(), true);
  EXPECT_CALL(*async_client_, dispatcher()).WillOnce(Return(nullptr));

  envoy::service::auth::v3::CheckRequest request;
  // if expectCallSend succeed with internal timer, while dispatcher is nullptr, it proves that the
  // timeout was set on the request.
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  // cancel to make sure the assert in the dtor is not triggered
  EXPECT_CALL(async_request_, cancel());
  client_->cancel();
}

// Test the client when an OK response is received with dynamic metadata in that OK response.
TEST_P(ExtAuthzGrpcClientTest, AuthorizationOkWithDynamicMetadata) {
  initialize(GetParam());

  auto check_response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = check_response->mutable_status();

  ProtobufWkt::Struct expected_dynamic_metadata;
  auto* metadata_fields = expected_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("true");
  check_response->mutable_dynamic_metadata()->MergeFrom(expected_dynamic_metadata);

  ProtobufWkt::Struct overridden_dynamic_metadata;
  metadata_fields = overridden_dynamic_metadata.mutable_fields();
  (*metadata_fields)["original"] = ValueUtil::stringValue("false");

  check_response->mutable_ok_response()->mutable_dynamic_metadata()->MergeFrom(
      overridden_dynamic_metadata);

  status->set_code(Grpc::Status::WellKnownGrpcStatus::Ok);

  // This is the expected authz response.
  auto authz_response = Response{};
  authz_response.status = CheckStatus::OK;
  authz_response.dynamic_metadata = overridden_dynamic_metadata;

  envoy::service::auth::v3::CheckRequest request;
  expectCallSend(request);
  client_->check(request_callbacks_, request, Tracing::NullSpan::instance(), stream_info_);

  Http::TestRequestHeaderMapImpl headers;
  client_->onCreateInitialMetadata(headers);

  EXPECT_CALL(span_, setTag(Eq("ext_authz_status"), Eq("ext_authz_ok")));
  EXPECT_CALL(request_callbacks_, onComplete_(WhenDynamicCastTo<ResponsePtr&>(
                                      AuthzResponseNoAttributes(authz_response))));
  client_->onSuccess(std::move(check_response), span_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
