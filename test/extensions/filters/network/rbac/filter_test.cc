#include "common/network/utility.h"

#include "extensions/filters/common/rbac/config.h"
#include "extensions/filters/network/rbac/rbac_filter.h"

#include "test/mocks/network/mocks.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

class RoleBasedAccessControlNetworkFilterTest : public testing::Test {
public:
  Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr setupConfig() {
    envoy::config::filter::network::rbac::v2::RBAC config;
    config.set_stat_prefix("tcp.");

    envoy::config::rbac::v2alpha::Policy policy;
    policy.add_permissions()->set_destination_port(123);
    // header and metadata rule should be ignored and always match.
    auto* ids = policy.add_principals()->mutable_and_ids();
    auto* header = ids->add_ids()->mutable_header();
    header->set_name("header");
    header->set_exact_match("value");
    auto* metadata = ids->add_ids()->mutable_metadata();
    metadata->set_filter("filter");
    metadata->add_path()->set_key("key");
    metadata->mutable_value()->set_bool_match(true);

    config.mutable_rules()->set_action(envoy::config::rbac::v2alpha::RBAC::ALLOW);
    (*config.mutable_rules()->mutable_policies())["foo"] = policy;

    envoy::config::rbac::v2alpha::Policy shadow_policy;
    shadow_policy.add_permissions()->set_destination_port(456);
    shadow_policy.add_principals()->set_any(true);
    config.mutable_shadow_rules()->set_action(envoy::config::rbac::v2alpha::RBAC::ALLOW);
    (*config.mutable_shadow_rules()->mutable_policies())["bar"] = shadow_policy;

    return std::make_shared<Filters::Common::RBAC::RoleBasedAccessControlFilterConfig>(config,
                                                                                       store_);
  }

  RoleBasedAccessControlNetworkFilterTest() : config_(setupConfig()) {
    filter_.reset(new RoleBasedAccessControlFilter(config_));
    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  void setDestinationPort(uint16_t port) {
    address_ = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", port, false);
    EXPECT_CALL(callbacks_.connection_, localAddress()).WillRepeatedly(ReturnRef(address_));
  }

  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
  Stats::IsolatedStoreImpl store_;
  Buffer::OwnedImpl data_;
  Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config_;

  std::unique_ptr<RoleBasedAccessControlFilter> filter_;
  Network::Address::InstanceConstSharedPtr address_;
};

TEST_F(RoleBasedAccessControlNetworkFilterTest, Allowed) {
  setDestinationPort(123);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().allowed_.value());
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
}

TEST_F(RoleBasedAccessControlNetworkFilterTest, Denied) {
  setDestinationPort(456);

  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, config_->stats().shadow_allowed_.value());
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
