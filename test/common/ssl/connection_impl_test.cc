#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/json/json_loader.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/ssl/connection_impl.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_impl.h"
#include "common/stats/stats_impl.h"

#include "test/common/ssl/ssl_certs_test.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Invoke;
using testing::_;

namespace Ssl {

namespace {

void testUtil(const std::string& client_ctx_json, const std::string& server_ctx_json,
              const std::string& expected_digest, const std::string& expected_uri,
              const Network::Address::IpVersion version) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(version), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection =
      dispatcher.createSslClientConnection(*client_ctx, socket.localAddress());
  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](uint32_t) -> void {
        if (!expected_digest.empty()) {
          EXPECT_EQ(expected_digest, server_connection->ssl()->sha256PeerCertificateDigest());
        }
        EXPECT_EQ(expected_uri, server_connection->ssl()->uriSanPeerCertificate());
        server_connection->close(Network::ConnectionCloseType::NoFlush);
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));
  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // namespace

class SslConnectionImplTest : public SslCertsTest,
                              public testing::WithParamInterface<Network::Address::IpVersion> {};

INSTANTIATE_TEST_CASE_P(IpVersions, SslConnectionImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SslConnectionImplTest, GetCertDigest) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json,
           "9d51ffbe193020e88ac2eb9072315e2e8bb3dac589041995b2af80ec7cb86de2", "", GetParam());
}

TEST_P(SslConnectionImplTest, GetUriWithUriSan) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_subject_alt_name": [ "istio:account1.foo.cluster.local" ]
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "istio:account1.foo.cluster.local", GetParam());
}

TEST_P(SslConnectionImplTest, GetNoUriWithDnsSan) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem"
  }
  )EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  // The SAN field only has DNS, expect "" for uriSanPeerCertificate().
  testUtil(client_ctx_json, server_ctx_json, "", "", GetParam());
}

TEST_P(SslConnectionImplTest, NoCert) {
  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "",
    "private_key_file": ""
  })EOF";

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": ""
  }
  )EOF";

  testUtil(client_ctx_json, server_ctx_json, "", "", GetParam());
}

TEST_P(SslConnectionImplTest, ClientAuthBadVerification) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_certificate_hash": "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  std::string client_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
  }
  )EOF";

  Json::ObjectSharedPtr client_ctx_loader = TestEnvironment::jsonLoadFromString(client_ctx_json);
  ContextConfigImpl client_ctx_config(*client_ctx_loader);
  ClientContextPtr client_ctx(manager.createSslClientContext(stats_store, client_ctx_config));
  Network::ClientConnectionPtr client_connection =
      dispatcher.createSslClientConnection(*client_ctx, socket.localAddress());
  client_connection->connect();

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](uint32_t) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST_P(SslConnectionImplTest, SslError) {
  Stats::IsolatedStoreImpl stats_store;
  Runtime::MockLoader runtime;

  std::string server_ctx_json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "verify_certificate_hash": "7B:0C:3F:0D:97:0E:FC:16:70:11:7A:0C:35:75:54:6B:17:AB:CF:20:D8:AA:A0:ED:87:08:0F:FB:60:4C:40:77"
  }
  )EOF";

  Json::ObjectSharedPtr server_ctx_loader = TestEnvironment::jsonLoadFromString(server_ctx_json);
  ContextConfigImpl server_ctx_config(*server_ctx_loader);
  ContextManagerImpl manager(runtime);
  ServerContextPtr server_ctx(manager.createSslServerContext(stats_store, server_ctx_config));

  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(Network::Test::getCanonicalLoopbackAddress(GetParam()), true);
  Network::MockListenerCallbacks callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher.createSslListener(connection_handler, *server_ctx, socket, callbacks, stats_store,
                                   Network::ListenerOptions::listenerOptionsWithBindToPort());

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection(socket.localAddress());
  client_connection->connect();
  Buffer::OwnedImpl bad_data("bad_handshake_data");
  client_connection->write(bad_data);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_connection_callbacks;
  EXPECT_CALL(callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_connection_callbacks);
      }));

  EXPECT_CALL(server_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](uint32_t) -> void {
        client_connection->close(Network::ConnectionCloseType::NoFlush);
        dispatcher.exit();
      }));

  dispatcher.run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(1UL, stats_store.counter("ssl.connection_error").value());
}

class SslReadBufferLimitTest : public SslCertsTest,
                               public testing::WithParamInterface<Network::Address::IpVersion> {
public:
  void Initialize(uint32_t read_buffer_limit) {
    server_ctx_loader_ = TestEnvironment::jsonLoadFromString(server_ctx_json_);
    server_ctx_config_.reset(new ContextConfigImpl(*server_ctx_loader_));
    manager_.reset(new ContextManagerImpl(runtime_));
    server_ctx_ = manager_->createSslServerContext(stats_store_, *server_ctx_config_);

    listener_ = dispatcher_.createSslListener(
        connection_handler_, *server_ctx_, socket_, listener_callbacks_, stats_store_,
        {.bind_to_port_ = true,
         .use_proxy_proto_ = false,
         .use_original_dst_ = false,
         .per_connection_buffer_limit_bytes_ = read_buffer_limit});

    client_ctx_loader_ = TestEnvironment::jsonLoadFromString(client_ctx_json_);
    client_ctx_config_.reset(new ContextConfigImpl(*client_ctx_loader_));
    client_ctx_ = manager_->createSslClientContext(stats_store_, *client_ctx_config_);

    client_connection_ =
        dispatcher_.createSslClientConnection(*client_ctx_, socket_.localAddress());
    client_connection_->connect();
    read_filter_.reset(new Network::MockReadFilter());
    client_connection_->addConnectionCallbacks(client_callbacks_);
    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::Connected));
  }

  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size,
                           uint32_t write_size, uint32_t num_writes, bool reserve_write_space) {
    Initialize(read_buffer_limit);

    EXPECT_CALL(listener_callbacks_, onNewConnection_(_))
        .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
          server_connection_ = std::move(conn);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->readBufferLimit());
        }));

    uint32_t filter_seen = 0;

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Network::FilterStatus {
          EXPECT_GE(expected_chunk_size, data.length());
          filter_seen += data.length();
          data.drain(data.length());
          if (filter_seen == (write_size * num_writes)) {
            server_connection_->close(Network::ConnectionCloseType::FlushWrite);
          }
          return Network::FilterStatus::StopIteration;
        }));

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](uint32_t) -> void {
          EXPECT_EQ((write_size * num_writes), filter_seen);
          dispatcher_.exit();
        }));

    for (uint32_t i = 0; i < num_writes; i++) {
      Buffer::OwnedImpl data(std::string(write_size, 'a'));

      // Incredibly contrived way of making sure that the write buffer has an empty chain in it.
      if (reserve_write_space) {
        Buffer::RawSlice iovecs[2];
        EXPECT_EQ(2UL, data.reserve(16384, iovecs, 2));
        iovecs[0].len_ = 0;
        iovecs[1].len_ = 0;
        data.commit(iovecs, 2);
      }

      client_connection_->write(data);
    }

    dispatcher_.run(Event::Dispatcher::RunType::Block);

    EXPECT_EQ(0UL, stats_store_.counter("ssl.connection_error").value());
  }

  void singleWriteTest(uint32_t read_buffer_limit, uint32_t bytes_to_write) {
    Initialize(read_buffer_limit);

    // For watermark testing, stick limits on the client connection as well.
    client_connection_->setWriteBufferWatermarks(read_buffer_limit / 2, read_buffer_limit + 1);
    int times_called = bytes_to_write > read_buffer_limit ? 1 : 0;
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(times_called);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(times_called);

    EXPECT_CALL(listener_callbacks_, onNewConnection_(_))
        .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
          server_connection_ = std::move(conn);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->readBufferLimit());
        }));

    std::unique_ptr<MockBuffer> buffer_ptr_{new MockBuffer()};
    MockBuffer& buffer_{*buffer_ptr_};
    dynamic_cast<ConnectionImpl*>(client_connection_.get())
        ->replaceWriteBufferForTest(std::move(buffer_ptr_));

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_)).Times(testing::AnyNumber());

    std::string data_to_write(bytes_to_write, 'a');
    Buffer::OwnedImpl buffer_to_write(data_to_write);
    std::string data_written;
    EXPECT_CALL(buffer_, move(_))
        .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                              Invoke(&buffer_, &MockBuffer::BaseMove)));
    EXPECT_CALL(buffer_, drain(_)).WillOnce(Invoke(&buffer_, &MockBuffer::BaseDrain));
    client_connection_->write(buffer_to_write);
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
    EXPECT_EQ(data_to_write, data_written);

    EXPECT_CALL(client_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    client_connection_->close(Network::ConnectionCloseType::NoFlush);
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }

  Stats::IsolatedStoreImpl stats_store_;
  Event::DispatcherImpl dispatcher_;
  Network::TcpListenSocket socket_{Network::Test::getCanonicalLoopbackAddress(GetParam()), true};
  Network::MockListenerCallbacks listener_callbacks_;
  Network::MockConnectionHandler connection_handler_;
  std::string server_ctx_json_ = R"EOF(
    {
      "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
      "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
      "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
    }
    )EOF";
  std::string client_ctx_json_ = R"EOF(
    {
      "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_cert.pem",
      "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/no_san_key.pem"
    }
  )EOF";
  Runtime::MockLoader runtime_;
  Json::ObjectSharedPtr server_ctx_loader_;
  std::unique_ptr<ContextConfigImpl> server_ctx_config_;
  std::unique_ptr<ContextManagerImpl> manager_;
  ServerContextPtr server_ctx_;
  Network::ListenerPtr listener_;
  Json::ObjectSharedPtr client_ctx_loader_;
  std::unique_ptr<ContextConfigImpl> client_ctx_config_;
  ClientContextPtr client_ctx_;
  Network::ClientConnectionPtr client_connection_;
  Network::ConnectionPtr server_connection_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  Network::MockConnectionCallbacks client_callbacks_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SslReadBufferLimitTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SslReadBufferLimitTest, NoLimit) {
  readBufferLimitTest(0, 256 * 1024, 256 * 1024, 1, false);
}

TEST_P(SslReadBufferLimitTest, NoLimitReserveSpace) { readBufferLimitTest(0, 512, 512, 1, true); }

TEST_P(SslReadBufferLimitTest, NoLimitSmallWrites) {
  readBufferLimitTest(0, 256 * 1024, 1, 256 * 1024, false);
}

TEST_P(SslReadBufferLimitTest, SomeLimit) {
  readBufferLimitTest(32 * 1024, 32 * 1024, 256 * 1024, 1, false);
}

TEST_P(SslReadBufferLimitTest, WritesSmallerThanWatermark) { singleWriteTest(5 * 1024, 1024); }

TEST_P(SslReadBufferLimitTest, WritesLargerThanWatermark) { singleWriteTest(1024, 5 * 1024); }

} // namespace Ssl
} // namespace Envoy
