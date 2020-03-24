#include "common/network/address_impl.h"
#include "common/network/filter_matcher.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {
struct CallbackHandle {
  std::unique_ptr<Network::MockListenerFilterCallbacks> callback_;
  std::unique_ptr<Network::MockConnectionSocket> socket_;
  Address::InstanceConstSharedPtr address_;
};
} // namespace
class SetListenerFilterMatcherTest : public testing::Test {
public:
  CallbackHandle createCallbackOnPort(int port) {
    CallbackHandle handle;
    handle.address_ = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", port);
    handle.socket_ = std::make_unique<MockConnectionSocket>();
    handle.callback_ = std::make_unique<MockListenerFilterCallbacks>();
    EXPECT_CALL(*handle.socket_, localAddress()).WillRepeatedly(ReturnRef(handle.address_));
    EXPECT_CALL(*handle.callback_, socket()).WillRepeatedly(ReturnRef(*handle.socket_));
    return handle;
  }
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate createPortPredicate(int port_start,
                                                                                     int port_end) {
    envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
    auto ports = pred.mutable_destination_port_range();
    ports->set_start(port_start);
    ports->set_end(port_end);
    return pred;
  }
};

TEST_F(SetListenerFilterMatcherTest, DstPortMatcher) {
  auto pred = createPortPredicate(80, 81);
  Network::SetListenerFilterMatcher matcher(pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_FALSE(matcher.matches(*handle79.callback_));
  EXPECT_TRUE(matcher.matches(*handle80.callback_));
  EXPECT_FALSE(matcher.matches(*handle81.callback_));
}

TEST_F(SetListenerFilterMatcherTest, TrueMatcher) {
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.set_any_match(true);
  Network::SetListenerFilterMatcher matcher(pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_TRUE(matcher.matches(*handle79.callback_));
  EXPECT_TRUE(matcher.matches(*handle80.callback_));
  EXPECT_TRUE(matcher.matches(*handle81.callback_));
}

TEST_F(SetListenerFilterMatcherTest, NotMatcher) {
  auto pred = createPortPredicate(80, 81);
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate not_pred;
  not_pred.mutable_not_match()->MergeFrom(pred);
  Network::SetListenerFilterMatcher matcher(not_pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_TRUE(matcher.matches(*handle79.callback_));
  EXPECT_FALSE(matcher.matches(*handle80.callback_));
  EXPECT_TRUE(matcher.matches(*handle81.callback_));
}

TEST_F(SetListenerFilterMatcherTest, OrMatcher) {
  auto pred80 = createPortPredicate(80, 81);
  auto pred443 = createPortPredicate(443, 444);

  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.mutable_or_match()->mutable_rules()->Add()->MergeFrom(pred80);
  pred.mutable_or_match()->mutable_rules()->Add()->MergeFrom(pred443);

  Network::SetListenerFilterMatcher matcher(pred);
  auto handle80 = createCallbackOnPort(80);
  auto handle443 = createCallbackOnPort(443);
  auto handle3306 = createCallbackOnPort(3306);

  EXPECT_FALSE(matcher.matches(*handle3306.callback_));
  EXPECT_TRUE(matcher.matches(*handle80.callback_));
  EXPECT_TRUE(matcher.matches(*handle443.callback_));
}

TEST_F(SetListenerFilterMatcherTest, AndMatcher) {
  auto pred80_3306 = createPortPredicate(80, 3306);
  auto pred443_3306 = createPortPredicate(443, 3306);

  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.mutable_and_match()->mutable_rules()->Add()->MergeFrom(pred80_3306);
  pred.mutable_and_match()->mutable_rules()->Add()->MergeFrom(pred443_3306);

  Network::SetListenerFilterMatcher matcher(pred);
  auto handle80 = createCallbackOnPort(80);
  auto handle443 = createCallbackOnPort(443);
  auto handle3306 = createCallbackOnPort(3306);

  EXPECT_FALSE(matcher.matches(*handle3306.callback_));
  EXPECT_FALSE(matcher.matches(*handle80.callback_));
  EXPECT_TRUE(matcher.matches(*handle443.callback_));
}
} // namespace Network
} // namespace Envoy