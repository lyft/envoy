#include <string>

#include "source/common/common/random_generator.h"
#include "source/extensions/private_key_providers/cryptomb/config.h"
#include "source/extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "fake_factory.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider
parsePrivateKeyProviderFromV3Yaml(const std::string& yaml_string) {
  envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider private_key_provider;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_string), private_key_provider);
  return private_key_provider;
}

class CryptoMbConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  CryptoMbConfigTest() : api_(Api::createApiForTest(store_, time_system_)) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(factory_context_, threadLocal()).WillByDefault(ReturnRef(tls_));
    ON_CALL(factory_context_, sslContextManager()).WillByDefault(ReturnRef(context_manager_));
    ON_CALL(context_manager_, privateKeyMethodManager())
        .WillByDefault(ReturnRef(private_key_method_manager_));
  }

  Ssl::PrivateKeyMethodProviderSharedPtr createWithConfig(std::string yaml,
                                                          bool supported_instruction_set = true) {
    FakeCryptoMbPrivateKeyMethodFactory cryptomb_factory(supported_instruction_set);
    Registry::InjectFactory<Ssl::PrivateKeyMethodProviderInstanceFactory>
        cryptomb_private_key_method_factory(cryptomb_factory);

    return factory_context_.sslContextManager()
        .privateKeyMethodManager()
        .createPrivateKeyMethodProvider(parsePrivateKeyProviderFromV3Yaml(yaml), factory_context_);
  }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Stats::IsolatedStoreImpl store_;
  Api::ApiPtr api_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Ssl::MockContextManager> context_manager_;
  TransportSockets::Tls::PrivateKeyMethodManagerImpl private_key_method_manager_;
};

TEST_F(CryptoMbConfigTest, CreateRsa1024) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-1024.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(true, provider->checkFips());
}

TEST_F(CryptoMbConfigTest, CreateRsa2048) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-2048.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(CryptoMbConfigTest, CreateRsa2048WithExponent3) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-2048-exponent-3.pem" }
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Only RSA keys with \"e\" parameter value 65537 are allowed, because "
                            "we can validate the signatures using multi-buffer instructions.");
}

TEST_F(CryptoMbConfigTest, CreateRsa3072) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-3072.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(CryptoMbConfigTest, CreateRsa4096) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem" }
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(CryptoMbConfigTest, CreateRsa512) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-512.pem" }
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Only RSA keys of 1024, 2048, 3072, and 4096 bits are supported.");
}

TEST_F(CryptoMbConfigTest, CreateEcdsaP256) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/ecdsa-p256.pem" }
)EOF";

  Ssl::PrivateKeyMethodProviderSharedPtr provider = createWithConfig(yaml);
  EXPECT_NE(nullptr, provider);
  EXPECT_EQ(true, provider->checkFips());
}

TEST_F(CryptoMbConfigTest, CreateEcdsaP256Inline) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key:
          inline_string: |
            -----BEGIN PRIVATE KEY-----
            MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgIxp5QZ3YFaT8s+CR
            rqUqeYSe5D9APgBZbyCvAkO2/JChRANCAARM53DFLHORcSyBpu5zpaG7/HfLXT8H
            r1RaoGEiH9pi3MIKg1H+b8EaM1M4wURT2yXMjuvogQ6ixs0B1mvRkZnL
            -----END PRIVATE KEY-----
)EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(CryptoMbConfigTest, CreateEcdsaP384) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/ecdsa-p384.pem" }
)EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Only P-256 ECDSA keys are supported.");
}

TEST_F(CryptoMbConfigTest, CreateMissingPrivateKey) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/missing.pem" }
)EOF";

  EXPECT_THROW(createWithConfig(yaml), EnvoyException);
}

TEST_F(CryptoMbConfigTest, CreateMissingKey) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        poll_delay: 0.02s
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml), EnvoyException,
                            "Unexpected DataSource::specifier_case(): 0");
}

TEST_F(CryptoMbConfigTest, CreateMissingPollDelay) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem" }
        )EOF";

  EXPECT_NE(nullptr, createWithConfig(yaml));
}

TEST_F(CryptoMbConfigTest, CreateNotSupportedInstructionSet) {
  const std::string yaml = R"EOF(
      provider_name: cryptomb
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.cryptomb.v3.CryptoMbPrivateKeyMethodConfig
        private_key: { "filename": "{{ test_rundir }}/test/extensions/private_key_providers/crypto_mb/test_data/rsa-4096.pem" }
        poll_delay: 0.02s
        )EOF";

  EXPECT_THROW_WITH_MESSAGE(createWithConfig(yaml, false), EnvoyException,
                            "Multi-buffer CPU instructions not available.");
}

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
