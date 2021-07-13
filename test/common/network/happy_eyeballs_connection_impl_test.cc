#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/mocks.h"

using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Network {

class HappyEyeballsConnectionImplTest : public testing::Test {
 public:
  HappyEyeballsConnectionImplTest()
      : failover_timer_(new testing::StrictMock<Event::MockTimer>(&dispatcher_)),
        transport_socket_options_(std::make_shared<TransportSocketOptionsImpl>()),
        options_(std::make_shared<ConnectionSocket::Options>()),
        address_list_({
          std::make_shared<Address::Ipv4Instance>("127.0.0.1"),
          std::make_shared<Address::Ipv4Instance>("127.0.0.2"),
          std::make_shared<Address::Ipv4Instance>("127.0.0.3") }) {
    EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
    EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[0], _, _, _)).WillOnce(
        testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));

    // This timer will be returned and armed as the happy eyeballs connection creates the next connection timer.
    EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
    EXPECT_CALL(*failover_timer_, enabled()).WillRepeatedly(Return(true));
    next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
    std::cerr << __LINE__ << std::endl;
    impl_ = std::make_unique<HappyEyeballsConnectionImpl>(dispatcher_,
                                                          address_list_,
                                                          Address::InstanceConstSharedPtr(),
                                                          transport_socket_factory_,
                                                          transport_socket_options_,
                                                          options_);
    std::cerr << __LINE__ << std::endl;

  }

  MockClientConnection* createNextConnection() {
    std::cerr << __LINE__ << " size: " << next_connections_.size() <<  std::endl;
    created_connections_.push_back(next_connections_.front().release());
    next_connections_.pop_front();
    EXPECT_CALL(*created_connections_.back(), addConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& cb) -> void { connection_callbacks_.push_back(&cb);}));
    return created_connections_.back();
  }

 protected:
  Event::MockDispatcher dispatcher_;
  testing::StrictMock<Event::MockTimer>* failover_timer_;
  MockTransportSocketFactory transport_socket_factory_;
  TransportSocketOptionsSharedPtr transport_socket_options_;
  const ConnectionSocket::OptionsSharedPtr options_;
  const std::vector<Address::InstanceConstSharedPtr> address_list_;
  std::vector<StrictMock<MockClientConnection>*> created_connections_;
  std::vector<ConnectionCallbacks*> connection_callbacks_;
  std::deque<std::unique_ptr<StrictMock<MockClientConnection>>> next_connections_;
  std::unique_ptr<HappyEyeballsConnectionImpl> impl_;
};

TEST_F(HappyEyeballsConnectionImplTest, Connect) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeout) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[1], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // Let the second attempt timeout to start the third and final attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  failover_timer_->invokeCallback();
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectFailed) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // When the first connection attempt fails, the next attempt will be immediately
  // started and the timer will be armed for the third attempe.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
}


TEST_F(HappyEyeballsConnectionImplTest, ConnectFirstSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenFirstSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[0], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondSuccess) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that calls are delegated to the right connection.
  EXPECT_CALL(*created_connections_[1], connecting()).WillOnce(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

TEST_F(HappyEyeballsConnectionImplTest, ConnectTimeoutThenSecondFailsAndFirstSucceeds) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[1], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // When the second attempt fails, the third and final attempt will be started.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(address_list_[2], _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, disableTimer());
  // Since there are no more address to connect to, the fallback timer will not
  // be rescheduled.
  ASSERT_EQ(2, created_connections_.size());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[1], close(ConnectionCloseType::NoFlush));
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
  connection_callbacks_[1]->onEvent(ConnectionEvent::RemoteClose);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";

  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
  connection_callbacks_[0]->onEvent(ConnectionEvent::RemoteClose);
  std::cerr << __FUNCTION__ << ":" << __LINE__ << " \n";
}

TEST_F(HappyEyeballsConnectionImplTest, NoDelay) {
  EXPECT_CALL(*created_connections_[0], noDelay(true));
  impl_->noDelay(true);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // noDelay() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), noDelay(true));
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that noDelay calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], noDelay(false));
  impl_->noDelay(false);
}

TEST_F(HappyEyeballsConnectionImplTest, DetectEarlyCloseWhenReadDisabled){
  EXPECT_CALL(*created_connections_[0], detectEarlyCloseWhenReadDisabled(true));
  impl_->detectEarlyCloseWhenReadDisabled(true);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // detectEarlyCloseWhenReadDisabled() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), detectEarlyCloseWhenReadDisabled(true));
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  // Verify that detectEarlyCloseWhenReadDisabled calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], detectEarlyCloseWhenReadDisabled(false));
  impl_->detectEarlyCloseWhenReadDisabled(false);
}

TEST_F(HappyEyeballsConnectionImplTest, AddReadFilter){
  MockReadFilterCallbacks callbacks;
  ReadFilterSharedPtr filter = std::make_shared<MockReadFilter>();
  filter->initializeReadFilterCallbacks(callbacks);
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addReadFilter(filter);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // addReadFilter() should be applied to the newly created connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  ReadFilterSharedPtr filter2 = std::make_shared<MockReadFilter>();
  filter2->initializeReadFilterCallbacks(callbacks);
  // Verify that detectEarlyCloseWhenReadDisabled calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], addReadFilter(filter2));
  impl_->addReadFilter(filter2);
}

TEST_F(HappyEyeballsConnectionImplTest, AddConnectionCallbacks){
  MockConnectionCallbacks callbacks;
  // The filter will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  // addConnectionCallbacks() should be applied to the newly created connection.
  EXPECT_CALL(*created_connections_[1], addConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& c) -> void {
    EXPECT_EQ(&c, &callbacks);
  }));
  //EXPECT_CALL(*created_connections_[1], addConnectionCallbacks(callbacks));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  MockConnectionCallbacks callbacks2;
  // Verify that detectEarlyCloseWhenReadDisabled calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[1], addConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& c) -> void {
    EXPECT_EQ(&c, &callbacks2);
  }));
  impl_->addConnectionCallbacks(callbacks2);
}

TEST_F(HappyEyeballsConnectionImplTest, RemoveConnectionCallbacks){
  MockConnectionCallbacks callbacks;
  MockConnectionCallbacks callbacks2;
  // The callbacks will be captured by the impl and not passed to the connection until it completes.
  impl_->addConnectionCallbacks(callbacks);
  impl_->addConnectionCallbacks(callbacks2);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  impl_->removeConnectionCallbacks(callbacks);

  // addConnectionCallbacks() should be applied to the newly created connection.
  EXPECT_CALL(*created_connections_[0], addConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& c) -> void {
    EXPECT_EQ(&c, &callbacks2);
  }));
  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  // Verify that removeConnectionCallbacks calls are delegated to the remaingin connection.
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_)).WillOnce(Invoke([&](ConnectionCallbacks& c) -> void {
    EXPECT_EQ(&c, &callbacks2);
  }));
  impl_->removeConnectionCallbacks(callbacks2);
}

TEST_F(HappyEyeballsConnectionImplTest, WriteBeforeConnect) {
  Buffer::OwnedImpl data("hello world");
  bool end_stream = false;

  impl_->write(data, end_stream);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  // The call to write() will be replayed on the underlying connection.
  EXPECT_CALL(*created_connections_[0], write(_, _)).WillOnce(
      Invoke([](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_EQ("hello world", data.toString());
        EXPECT_FALSE(end_stream);
        ;})
                                                              );
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);
}


struct MockConnectionStats {
  Connection::ConnectionStats toBufferStats() {
    return {rx_total_,   rx_current_,   tx_total_,
            tx_current_, &bind_errors_, &delayed_close_timeouts_};
  }

  StrictMock<Stats::MockCounter> rx_total_;
  StrictMock<Stats::MockGauge> rx_current_;
  StrictMock<Stats::MockCounter> tx_total_;
  StrictMock<Stats::MockGauge> tx_current_;
  StrictMock<Stats::MockCounter> bind_errors_;
  StrictMock<Stats::MockCounter> delayed_close_timeouts_;
};

TEST_F(HappyEyeballsConnectionImplTest, SetConnectionStats){
  MockConnectionStats stats;
  Connection::ConnectionStats cs = { stats.rx_total_, stats.rx_current_, stats.tx_total_, stats.tx_current_, &stats.bind_errors_, &stats.delayed_close_timeouts_};
  EXPECT_CALL(*created_connections_[0], setConnectionStats(_)).WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void {
    EXPECT_EQ(&s, &cs);
  }));
  impl_->setConnectionStats(cs);

  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  // Let the first attempt timeout to start the second attempt.
  next_connections_.push_back(std::make_unique<StrictMock<MockClientConnection>>());
  EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(
      testing::InvokeWithoutArgs(this, &HappyEyeballsConnectionImplTest::createNextConnection));
  EXPECT_CALL(*next_connections_.back(), connect());
  // setConnectionStats() should be applied to the newly created connection.
  EXPECT_CALL(*next_connections_.back(), setConnectionStats(_)).WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void {
    EXPECT_EQ(&s, &cs);
  }));
  EXPECT_CALL(*failover_timer_, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(1);
  failover_timer_->invokeCallback();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[1], removeConnectionCallbacks(_));
  EXPECT_CALL(*created_connections_[0], close(ConnectionCloseType::NoFlush));
  connection_callbacks_[1]->onEvent(ConnectionEvent::Connected);

  /*
  // Verify that setConnectionStats calls are delegated to the remaingin connection.
  MockConnectionStats stats2;
  EXPECT_CALL(*created_connections_[1], setConnectionStats(_)).WillOnce(Invoke([&](const Connection::ConnectionStats& s) -> void {
    EXPECT_EQ(&s, &stats2);
  }));
  impl_->setConnectionStats(stats2);
  */
}

TEST_F(HappyEyeballsConnectionImplTest, state) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*created_connections_[0], state()).WillRepeatedly(Return(Connection::State::Open));
  EXPECT_EQ(Connection::State::Open, impl_->state());

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], state()).WillOnce(Return(Connection::State::Closing));
  EXPECT_EQ(Connection::State::Closing, impl_->state());
}

TEST_F(HappyEyeballsConnectionImplTest, connecting) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*created_connections_[0], connecting()).WillRepeatedly(Return(true));
  EXPECT_TRUE(impl_->connecting());

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], connecting()).WillRepeatedly(Return(false));
  EXPECT_FALSE(impl_->connecting());
}

// Tests for HappyEyeballsConnectionImpl methods which must only be called after connect()
// has finised.

TEST_F(HappyEyeballsConnectionImplTest, addWriteFilter) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  MockWriteFilterCallbacks callbacks;
  WriteFilterSharedPtr filter = std::make_shared<MockWriteFilter>();
  filter->initializeWriteFilterCallbacks(callbacks);
  EXPECT_CALL(*created_connections_[0], addWriteFilter(filter));
  impl_->addWriteFilter(filter);
}

TEST_F(HappyEyeballsConnectionImplTest, addFilter) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  MockReadFilterCallbacks read_callbacks;
  MockWriteFilterCallbacks write_callbacks;
  FilterSharedPtr filter = std::make_shared<MockFilter>();
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);
  EXPECT_CALL(*created_connections_[0], addFilter(filter));
  impl_->addFilter(filter);
}

TEST_F(HappyEyeballsConnectionImplTest, addBytesSentCallback) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  std::function<bool(uint64_t)> cb = [](uint64_t) { return true; };
  EXPECT_CALL(*created_connections_[0], addBytesSentCallback(_));
  impl_->addBytesSentCallback(cb);
}

TEST_F(HappyEyeballsConnectionImplTest, enableHalfClose) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], enableHalfClose(true));
  impl_->enableHalfClose(true);
}

TEST_F(HappyEyeballsConnectionImplTest, isHalfCloseEnabled) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], isHalfCloseEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->isHalfCloseEnabled());
}

TEST_F(HappyEyeballsConnectionImplTest, nextProtocol) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], nextProtocol()).WillOnce(Return("h3"));
  EXPECT_EQ("h3", impl_->nextProtocol());
}

TEST_F(HappyEyeballsConnectionImplTest, readDisable) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], readDisable(true));
  impl_->readDisable(true);
}

TEST_F(HappyEyeballsConnectionImplTest, readEnabled) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], readEnabled()).WillOnce(Return(true));
  EXPECT_TRUE(impl_->readEnabled());
}

TEST_F(HappyEyeballsConnectionImplTest, addressProvider) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);


  const SocketAddressSetterImpl provider(std::make_shared<Address::Ipv4Instance>(80),
                                   std::make_shared<Address::Ipv4Instance>(80));
  EXPECT_CALL(*created_connections_[0], addressProvider()).WillOnce(ReturnRef(provider));
  impl_->addressProvider();
}

TEST_F(HappyEyeballsConnectionImplTest, addressProviderSharedPtr) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);


  SocketAddressProviderSharedPtr provider = std::make_shared<SocketAddressSetterImpl>(std::make_shared<Address::Ipv4Instance>(80),
                                                                                      std::make_shared<Address::Ipv4Instance>(80));
  EXPECT_CALL(*created_connections_[0], addressProviderSharedPtr()).WillOnce(Return(provider));
  EXPECT_EQ(provider, impl_->addressProviderSharedPtr());
}

TEST_F(HappyEyeballsConnectionImplTest, unixSocketPeerCredentials) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);


  EXPECT_CALL(*created_connections_[0], unixSocketPeerCredentials()).WillOnce(Return(absl::optional<Connection::UnixDomainSocketPeerCredentials>()));
  EXPECT_FALSE(impl_->unixSocketPeerCredentials().has_value());
}

TEST_F(HappyEyeballsConnectionImplTest, ssl) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);


  Ssl::ConnectionInfoConstSharedPtr ssl = nullptr;
  EXPECT_CALL(*created_connections_[0], ssl()).WillOnce(Return(ssl));
  EXPECT_EQ(ssl, impl_->ssl());
}

TEST_F(HappyEyeballsConnectionImplTest, setBufferLimits) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], setBufferLimits(42));
  impl_->setBufferLimits(42);
}

TEST_F(HappyEyeballsConnectionImplTest, requestedServerName) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], requestedServerName()).WillOnce(Return("name"));
  EXPECT_EQ("name", impl_->requestedServerName());
}

TEST_F(HappyEyeballsConnectionImplTest, setDelayedCloseTimeout) {
  EXPECT_CALL(*created_connections_[0], connect());
  impl_->connect();

  EXPECT_CALL(*failover_timer_, disableTimer());
  EXPECT_CALL(*created_connections_[0], removeConnectionCallbacks(_));
  connection_callbacks_[0]->onEvent(ConnectionEvent::Connected);

  EXPECT_CALL(*created_connections_[0], setDelayedCloseTimeout(std::chrono::milliseconds(5)));
  EXPECT_EQ("name", impl_->setDelayedCloseTimeout(std::chrono::milliseconds(5)));
}

} // namespace Network
} // namespace Envoy
