#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.h"
#include "envoy/config/transport_socket/raw_buffer/v2/raw_buffer.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "common/network/connection_impl.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/transport_sockets/raw_buffer/config.h"

#include "test/config/utility.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/starttls_integration_test.pb.h"
#include "test/integration/starttls_integration_test.pb.validate.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {

// Simple filter for test purposes. This filter will be injected into the filter chain during
// tests. The filter reacts only to few keywords. If received payload does not contain
// allowed keyword, filter will stop iteration.
// The filter will be configured to sit on top of tcp_proxy and use start-tls transport socket.
// If it receives a data which is not known keyword it means that transport socket has not been
// successfully converted to use TLS and filter receives either encrypted data or TLS handshake
// messages.
class StartTlsSwitchFilter : public Network::Filter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  bool onCommand(Buffer::Instance& buf);

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  // Filter will allow only the following messages to pass.
  std::set<std::string> allowed_messages_{"hello", "switch", "hola", "bye"};
};

Network::FilterStatus StartTlsSwitchFilter::onNewConnection() {
  return Network::FilterStatus::Continue;
}

// Method checks the received payload.
bool StartTlsSwitchFilter::onCommand(Buffer::Instance& buf) {
  const std::string message = buf.toString();
  if (allowed_messages_.find(message) == allowed_messages_.end()) {
    return false;
  }
  if (message == "switch") {
    if (read_callbacks_->connection().transportProtocol() ==
        Extensions::TransportSockets::TransportProtocolNames::get().StartTls) {
      read_callbacks_->connection().startSecureTransport();
    }
  }
  return true;
}

Network::FilterStatus StartTlsSwitchFilter::onData(Buffer::Instance& buf, bool) {
  return onCommand(buf) ? Network::FilterStatus::Continue : Network::FilterStatus::StopIteration;
}

// Network::WriteFilter
Network::FilterStatus StartTlsSwitchFilter::onWrite(Buffer::Instance& buf, bool) {
  return onCommand(buf) ? Network::FilterStatus::Continue : Network::FilterStatus::StopIteration;
}

// Config factory for StartTlsSwitchFilter.
class StartTlsSwitchFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                              test::integration::starttls::StartTlsFilterConfig> {
public:
  explicit StartTlsSwitchFilterConfigFactory(const std::string& name) : FactoryBase(name) {}

  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::starttls::StartTlsFilterConfig&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<StartTlsSwitchFilter>());
    };
  }

  std::string name() const override { return name_; }

private:
  const std::string name_;
};

// ClientTestConnection is used for simulating a client
// which initiates a connection to Envoy in clear-text and then switches to TLS
// without closing the socket.
class ClientTestConnection : public Network::ClientConnectionImpl {
public:
  ClientTestConnection(Event::Dispatcher& dispatcher,
                       const Network::Address::InstanceConstSharedPtr& remote_address,
                       const Network::Address::InstanceConstSharedPtr& source_address,
                       Network::TransportSocketPtr&& transport_socket,
                       const Network::ConnectionSocket::OptionsSharedPtr& options)
      : ClientConnectionImpl(dispatcher, remote_address, source_address,
                             std::move(transport_socket), options) {}

  void setTransportSocket(Network::TransportSocketPtr&& transport_socket) {
    transport_socket_ = std::move(transport_socket);
    transport_socket_->setTransportSocketCallbacks(*this);

    // Reset connection's state machine.
    connecting_ = true;

    // Issue event which will trigger TLS handshake.
    ioHandle().activateFileEvents(Event::FileReadyType::Write);
  }
};

// Fixture class for integration tests.
class StartTlsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public BaseIntegrationTest {
public:
  StartTlsIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::startTlsConfig()) {}
  void initialize() override;
  void addStartTlsSwitchFilter(ConfigHelper& config_helper);

  // Contexts needed by raw buffer and tls transport sockets.
  std::unique_ptr<Ssl::ContextManager> tls_context_manager_;
  Network::TransportSocketFactoryPtr tls_context_;
  Network::TransportSocketFactoryPtr cleartext_context_;

  MockWatermarkBuffer* client_write_buffer_{nullptr};
  ConnectionStatusCallbacks connect_callbacks_;

  // Config factory for StartTlsSwitchFilter.
  StartTlsSwitchFilterConfigFactory config_factory_{"startTls"};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_config_factory_{config_factory_};
};

void StartTlsIntegrationTest::initialize() {
  EXPECT_CALL(*mock_buffer_factory_, create_(_, _, _))
      .Times(1)
      .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                           std::function<void()> above_overflow) -> Buffer::Instance* {
        client_write_buffer_ =
            new NiceMock<MockWatermarkBuffer>(below_low, above_high, above_overflow);
        ON_CALL(*client_write_buffer_, move(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
        ON_CALL(*client_write_buffer_, drain(_))
            .WillByDefault(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
        return client_write_buffer_;
      }));
  config_helper_.renameListener("tcp_proxy");
  addStartTlsSwitchFilter(config_helper_);

  // Setup factories and contexts for upstream clear-text raw buffer transport socket.
  auto config = std::make_unique<envoy::config::transport_socket::raw_buffer::v2::RawBuffer>();

  auto factory =
      std::make_unique<Extensions::TransportSockets::RawBuffer::UpstreamRawBufferSocketFactory>();
  cleartext_context_ = Network::TransportSocketFactoryPtr{
      factory->createTransportSocketFactory(*config, factory_context_)};

  // Setup factories and contexts for tls transport socket.
  tls_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  tls_context_ = Ssl::createClientSslTransportSocketFactory({}, *tls_context_manager_, *api_);

  BaseIntegrationTest::initialize();
}

// Method adds StartTlsSwitchFilter into the filter chain.
// The filter is required to instruct StartTls transport
// socket to start using Tls.
void StartTlsIntegrationTest::addStartTlsSwitchFilter(ConfigHelper& config_helper) {
  config_helper.addNetworkFilter(R"EOF(
      name: startTls
      typed_config:
        "@type": type.googleapis.com/test.integration.starttls.StartTlsFilterConfig
    )EOF");
  // double-check the filter was actually added
  config_helper.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ASSERT_EQ("startTls",
              bootstrap.static_resources().listeners(0).filter_chains(0).filters(0).name());
  });
}

// Test creates a client clear-text connection to Envoy and sends several messages.
// Then a special message is sent, which causes StartTlsSwitchFilter to
// instruct StartTls transport socket to start using tls.
// The Client connection starts using tls, performs tls handshake and few messages
// are sent over tls.
TEST_P(StartTlsIntegrationTest, SwitchToTlsTest) {
  initialize();

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("tcp_proxy"));
  std::unique_ptr<ClientTestConnection> conn = std::make_unique<ClientTestConnection>(
      *dispatcher_, address, Network::Address::InstanceConstSharedPtr(),
      cleartext_context_->createTransportSocket(
          std::make_shared<Network::TransportSocketOptionsImpl>(
              absl::string_view(""), std::vector<std::string>(), std::vector<std::string>())),
      nullptr);

  conn->enableHalfClose(true);
  conn->addConnectionCallbacks(connect_callbacks_);

  // Open clear-text connection.
  conn->connect();

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_THAT(test_server_->server().listenerManager().numConnections(), 1);

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  conn->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 5) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send a message to switch to tls on the receiver side.
  // StartTlsSwitchFilter will switch transport socket on the
  // receiver side upon receiving "switch" message.
  buffer.add("switch");
  conn->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 11) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  // Without closing the connection, switch to tls.
  conn->setTransportSocket(
      tls_context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(),
          std::vector<std::string>{"envoyalpn"})));
  connect_callbacks_.reset();
  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Send few messages over encrypted connection.
  buffer.add("hola");
  conn->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 15) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(15));

  buffer.add("bye");
  conn->write(buffer, false);
  while (client_write_buffer_->bytesDrained() != 18) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Make sure the data makes it upstream.
  ASSERT_TRUE(fake_upstream_connection->waitForData(18));

  conn->close(Network::ConnectionCloseType::FlushWrite);
}

INSTANTIATE_TEST_SUITE_P(StartTlsIntegrationTestSuite, StartTlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

} // namespace Envoy
