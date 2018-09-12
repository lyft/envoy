#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {
const char AllWithAny[] = R"(
providers:
  provider_1:
    issuer: iss_1
  provider_2:
    issuer: iss_2
  provider_3:
    issuer: iss_3
rules:
- match: { path: "/" }
  requires:
    requires_all:
      requirements:
      - requires_any:
          requirements:
            - provider_name: "provider_1"
            - provider_name: "provider_2"
      - provider_name: "provider_3"
)";

const char AnyWithAll[] = R"(
providers:
  provider_1:
    issuer: iss_1
  provider_2:
    issuer: iss_2
  provider_3:
    issuer: iss_3
  provider_4:
    issuer: iss_4
rules:
- match: { path: "/" }
  requires:
    requires_any:
      requirements:
      - requires_all:
          requirements:
            - provider_name: "provider_1"
            - provider_name: "provider_2"
      - requires_all:
          requirements:
            - provider_name: "provider_3"
            - provider_name: "provider_4"
)";

typedef std::unordered_map<std::string, const Status&> StatusMap;

constexpr auto allowfailed = "_allow_failed_";

class GroupVerifierTest : public ::testing::Test {
public:
  void createVerifier() {
    ON_CALL(mock_factory_, create(_, _, _))
        .WillByDefault(Invoke(
            [&](const CheckAudienceConstSharedPtr, const absl::optional<std::string>& issuer,
                bool) { return std::move(mock_auths_[issuer ? issuer.value() : allowfailed]); }));
    verifier_ = Verifier::create(proto_config_.rules()[0].requires(), proto_config_.providers(),
                                 mock_factory_, mock_extractor_);
    ON_CALL(mock_extractor_, extract(_)).WillByDefault(Invoke([](const Http::HeaderMap&) {
      return std::vector<JwtLocationConstPtr>{};
    }));
  }
  void createSyncMockAuthsAndVerifier(const StatusMap& statuses) {
    for (const auto& it : statuses) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
          .WillOnce(
              Invoke([status = it.second](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                          AuthenticatorCallback callback) { callback(status); }));
      mock_auths_[it.first] = std::move(mock_auth);
    }
    createVerifier();
  }

  std::unordered_map<std::string, AuthenticatorCallback>
  createAsyncMockAuthsAndVerifier(const std::vector<std::string>& issuers) {
    std::unordered_map<std::string, AuthenticatorCallback> callbacks;
    for (std::size_t i = 0; i < issuers.size(); ++i) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
          .WillOnce(Invoke([&callbacks, iss = issuers[i]](Http::HeaderMap&,
                                                          std::vector<JwtLocationConstPtr>*,
                                                          AuthenticatorCallback callback) {
            callbacks[iss] = std::move(callback);
          }));
      mock_auths_[issuers[i]] = std::move(mock_auth);
    }
    createVerifier();
    return callbacks;
  }

  JwtAuthentication proto_config_;
  VerifierPtr verifier_;
  MockVerifierCallbacks mock_cb_;
  std::unordered_map<std::string, std::unique_ptr<MockAuthenticator>> mock_auths_;
  NiceMock<MockAuthFactory> mock_factory_;
  ContextSharedPtr context_;
  NiceMock<MockExtractor> mock_extractor_;
};

// Deeply nested anys that ends in provider name
TEST_F(GroupVerifierTest, DeeplyNestedAnys) {
  const char config[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    forward_payload_header: sec-istio-auth-userinfo
    from_params:
    - jwta
    - jwtb
    - jwtc
rules:
- match: { path: "/match" }
  requires:
    requires_any:
      requirements:
      - requires_any:
          requirements:
          - requires_any:
              requirements:
              - provider_name: "example_provider"
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"https://example.com", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"sec-istio-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// require alls that just ends
TEST_F(GroupVerifierTest, CanHandleUnexpectedEnd) {
  const char config[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    forward_payload_header: sec-istio-auth-userinfo
rules:
- match: { path: "/match" }
  requires:
    requires_all:
      requirements:
      - requires_all:
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _)).Times(0);
  mock_auths_["https://example.com"] = std::move(mock_auth);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// test requires all with both auth returning OK
TEST_F(GroupVerifierTest, TestRequiresAll) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"https://example.com", Status::Ok}, {"https://other.com", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with first auth returning bad format
TEST_F(GroupVerifierTest, TestRequiresAllBadFormat) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"https://example.com", "https://other.com"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["https://example.com"](Status::JwtBadFormat);
  // can keep invoking callback
  callbacks["https://other.com"](Status::Ok);
  callbacks["https://example.com"](Status::Ok);
  callbacks["https://other.com"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with second auth returning missing jwt
TEST_F(GroupVerifierTest, TestRequiresAllMissing) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"https://example.com", "https://other.com"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["https://example.com"](Status::Ok);
  callbacks["https://other.com"](Status::JwtMissed);
  // can keep invoking callback
  callbacks["https://example.com"](Status::Ok);
  callbacks["https://other.com"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requrires all and mock auths simulate cache misses and async return of failure statuses.
TEST_F(GroupVerifierTest, TestRequiresAllBothFailed) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"https://example.com", "https://other.com"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
  callbacks["https://example.com"](Status::JwtUnknownIssuer);
  callbacks["https://other.com"](Status::JwtUnknownIssuer);
}

// Test requires any with first auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyFirstAuthOK) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"https://example.com", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

// Test requires any with last auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyLastAuthOk) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"https://example.com", Status::JwtUnknownIssuer},
                                           {"https://other.com", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires any with both auth returning error. Requires any returns the error last recieved
// back to the caller.
TEST_F(GroupVerifierTest, TestRequiresAnyAllAuthFailed) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  createSyncMockAuthsAndVerifier(StatusMap{{"https://example.com", Status::JwtHeaderBadKid},
                                           {"https://other.com", Status::JwtUnknownIssuer}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllFirstAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"iss_1", Status::Ok}, {"iss_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllLastAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"iss_1", Status::JwtUnknownIssuer}, {"iss_2", Status::Ok}, {"iss_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  auto callbacks =
      createAsyncMockAuthsAndVerifier(std::vector<std::string>{"iss_1", "iss_2", "iss_3"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["iss_1"](Status::Ok);
  callbacks["iss_2"](Status::Ok);
  callbacks["iss_3"](Status::Ok);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any failed and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyFailed) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  auto callbacks =
      createAsyncMockAuthsAndVerifier(std::vector<std::string>{"iss_1", "iss_2", "iss_3"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["iss_1"](Status::JwksFetchFail);
  callbacks["iss_2"](Status::JwksFetchFail);
  callbacks["iss_3"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. Mock auths simulate JWKs cache
// hits and inline return of errors. Requires any returns the error last recieved back to the
// caller.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllFailed) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"iss_1", Status::JwksFetchFail}, {"iss_3", Status::JwtExpired}});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a requires any which in turn has 2 requires all. The first inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyFirstAllIsOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks =
      createAsyncMockAuthsAndVerifier(std::vector<std::string>{"iss_1", "iss_2", "iss_3", "iss_4"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["iss_2"](Status::Ok);
  callbacks["iss_3"](Status::JwtMissed);
  callbacks["iss_1"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. The last inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyLastAllIsOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks =
      createAsyncMockAuthsAndVerifier(std::vector<std::string>{"iss_1", "iss_2", "iss_3", "iss_4"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["iss_3"](Status::Ok);
  callbacks["iss_4"](Status::Ok);
  callbacks["iss_2"](Status::JwtExpired);
}

// Test contains a requires any which in turn has 2 requires all. The both inner requires all are
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllAreOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks =
      createAsyncMockAuthsAndVerifier(std::vector<std::string>{"iss_1", "iss_2", "iss_3", "iss_4"});

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["iss_1"](Status::Ok);
  callbacks["iss_2"](Status::Ok);
  callbacks["iss_3"](Status::Ok);
  callbacks["iss_4"](Status::Ok);
}

// Test require any with additional allow all
TEST_F(GroupVerifierTest, TestRequiresAnyWithAllowAll) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  proto_config_.mutable_rules(0)
      ->mutable_requires()
      ->mutable_requires_any()
      ->add_requirements()
      ->mutable_allow_missing_or_failed();

  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"https://example.com", "https://other.com"});
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillOnce(Invoke(
          [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*, AuthenticatorCallback callback) {
            callbacks[allowfailed] = std::move(callback);
          }));
  mock_auths_[allowfailed] = std::move(mock_auth);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[allowfailed](Status::Ok);
  // with requires any, if any inner verifier returns OK the whole any verifier should return OK.
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
