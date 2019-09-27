#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/autonomous_upstream.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

// TODO(incfly):
// - upstream setup, finish multiple endpiont upstream setup.
//    use autonomous_upstream_ = true... otherwise too complicated.
//   for now wait on 'waitforindex 0' hardcoded.
//   blocking on the failure of the multi endpoints same ssl context.
//   maybe using some route api to achieve.
// - Client envoy configuration modifying, use matcher!
// bazel test //test/integration:transport_socket_match_integration_test --test_output=streamed
// --test_arg='-l info'
class TransportSockeMatchIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  TransportSockeMatchIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1,
                            TestEnvironment::getIpVersionsForTest().front(),
                            ConfigHelper::HTTP_PROXY_CONFIG),
        num_hosts_{2} {
    autonomous_upstream_ = true;
    setUpstreamCount(num_hosts_);
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      auto* cluster = static_resources->mutable_clusters(0);
      auto* common_tls_context = cluster->mutable_tls_context()->mutable_common_tls_context();
      auto* tls_cert = common_tls_context->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
      tls_cert->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      // Setup the client Envoy TLS config.
      cluster->clear_hosts();
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      for (uint32_t i = 0; i < num_hosts_; i++) {
        auto* lb_endpoint = endpoints->add_lb_endpoints();
        // ConfigHelper will fill in ports later.
        auto* endpoint = lb_endpoint->mutable_endpoint();
        auto* addr = endpoint->mutable_address()->mutable_socket_address();
        addr->set_address(Network::Test::getLoopbackAddressString(
            TestEnvironment::getIpVersionsForTest().front()));
        addr->set_port_value(0);
        // Assign type metadata based on i.
        // auto* metadata = lb_endpoint->mutable_metadata();
        // Envoy::Config::Metadata::mutableMetadataValue(*metadata, "envoy.lb", type_key_)
        //.set_string_value((i % 2 == 0) ? "a" : "b");
      }
    });
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    // copied from grpc_*_hardness.*.h
    envoy::api::v2::auth::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    common_tls_context->add_alpn_protocols("h2");
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
    // if (use_client_cert_) {
    tls_context.mutable_require_client_certificate()->set_value(true);
    auto* validation_context = common_tls_context->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    //}
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);
    static Stats::Scope* upstream_stats_store = new Stats::IsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  void createUpstreams() override {
	 for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
			auto endpoint = upstream_address_fn_(i);
      // Makes difference, 3/10 requests are failing.
      if (i%2 == 0 || i % 2 == 1) {
      fake_upstreams_.emplace_back(
          new AutonomousUpstream(createUpstreamSslContext(), endpoint->ip()->port(),
                FakeHttpConnection::Type::HTTP1, endpoint->ip()->version(),timeSystem()));
      } else {
        fake_upstreams_.emplace_back(
            new AutonomousUpstream(Network::Test::createRawBufferSocketFactory(), endpoint->ip()->port(),
                  FakeHttpConnection::Type::HTTP1, endpoint->ip()->version(),timeSystem()));
      }
		}
  }

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP1);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  }

  const uint32_t num_hosts_;
};

TEST_F(TransportSockeMatchIntegrationTest, BasicMatch) {
	initialize();
	codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},  {":path", "/test"},
                                                {":scheme", "http"}, {":authority", "host"},
                                                {"x-type", "b"},     {"x-hash", "hash-b"}};

	Http::TestHeaderMapImpl response_headers{{":status", "200"}};

	// Send header only request.
	IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
	response->waitForEndStream();
	EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  //IntegrationStreamDecoderPtr response;
	//response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
	//}
	// TODO: get this back, protocol_integration_test.cc, subset_lb_integrationt-test.cc
	//std::vector<int> index;
	//for (uint32_t i = 0; i < num_hosts_; i++) { index.push_back(i); }
	//waitForNextUpstreamRequest(0);
	//// Send response headers, and end_stream if there is no response body.
	//upstream_request_->encodeHeaders(default_response_headers_, true);
	//// Wait for the response to be read by the codec client.
	//response->waitForEndStream();
	//EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

} // namespace Envoy
