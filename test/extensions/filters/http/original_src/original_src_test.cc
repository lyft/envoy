#include "envoy/config/filter/http/original_src/v2alpha1/original_src.pb.h"

#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/http/original_src/original_src.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Exactly;
using testing::SaveArg;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OriginalSrc {
namespace {

class OriginalSrcTest : public testing::Test {
public:
  std::unique_ptr<OriginalSrcFilter> makeDefaultFilter() {
    return makeFilterWithCallbacks(callbacks_);
  }

  std::unique_ptr<OriginalSrcFilter>
  makeFilterWithCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    Config default_config;
    auto filter = std::make_unique<OriginalSrcFilter>(default_config);
    filter->setDecoderFilterCallbacks(callbacks);
    return filter;
  }

  std::unique_ptr<OriginalSrcFilter> makeMarkingFilter(uint32_t mark) {
    envoy::config::filter::http::original_src::v2alpha1::OriginalSrc proto_config;
    proto_config.set_mark(mark);

    Config config(proto_config);
    auto filter = std::make_unique<OriginalSrcFilter>(config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  void setAddressToReturn(const std::string& address) {
    callbacks_.stream_info_.downstream_remote_address_ = Network::Utility::resolveUrl(address);
  }

protected:
  StrictMock<MockBuffer> buffer_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnectionSocket> socket_;
  Http::TestHeaderMapImpl headers_;

  absl::optional<Network::Socket::Option::Details>
  findOptionDetails(const Network::Socket::Options& options, Network::SocketOptionName name,
                    envoy::api::v2::core::SocketOption::SocketState state) {
    for (const auto& option : options) {
      auto details = option->getOptionDetails(socket_, state);
      if (details.has_value() && details->name_ == name) {
        return details;
      }
    }

    return absl::nullopt;
  }
};

TEST_F(OriginalSrcTest, onNonIpAddressDecodeSkips) {
  auto filter = makeDefaultFilter();
  setAddressToReturn("unix://domain.socket");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

TEST_F(OriginalSrcTest, decodeHeadersIpv4AddressAddsOption) {
  auto filter = makeDefaultFilter();

  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);

  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  ASSERT_NE(options->at(0), nullptr);

  NiceMock<Network::MockConnectionSocket> socket;
  EXPECT_CALL(socket,
              setLocalAddress(PointeesEq(callbacks_.stream_info_.downstream_remote_address_)));
  options->at(0)->setOption(socket, envoy::api::v2::core::SocketOption::STATE_PREBIND);
}

TEST_F(OriginalSrcTest, decodeHeadersIpv4AddressUsesCorrectAddress) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:0");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);
  std::vector<uint8_t> key;
  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  options->at(0)->hashKey(key);
  std::vector<uint8_t> expected_key = {1, 2, 3, 4};

  EXPECT_EQ(key, expected_key);
}

TEST_F(OriginalSrcTest, decodeHeadersIpv4AddressBleachesPort) {
  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  NiceMock<Network::MockConnectionSocket> socket;
  const auto expected_address = Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(socket, setLocalAddress(PointeesEq(expected_address)));

  // not ideal -- we're assuming that the original_src option is first, but it's a fair assumption
  // for now.
  options->at(0)->setOption(socket, envoy::api::v2::core::SocketOption::STATE_PREBIND);
}

TEST_F(OriginalSrcTest, filterAddsTransparentOption) {
  if (!ENVOY_SOCKET_IP_TRANSPARENT.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeDefaultFilter();
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  auto transparent_option = findOptionDetails(*options, ENVOY_SOCKET_IP_TRANSPARENT,
                                              envoy::api::v2::core::SocketOption::STATE_PREBIND);

  EXPECT_TRUE(transparent_option.has_value());
}

TEST_F(OriginalSrcTest, filterAddsMarkOption) {
  if (!ENVOY_SOCKET_SO_MARK.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(1234);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                       envoy::api::v2::core::SocketOption::STATE_PREBIND);

  ASSERT_TRUE(mark_option.has_value());
  uint32_t value = 1234;
  absl::string_view value_as_bstr(reinterpret_cast<const char*>(&value), sizeof(value));
  EXPECT_EQ(value_as_bstr, mark_option->value_);
}

TEST_F(OriginalSrcTest, Mark0NotAdded) {
  if (!ENVOY_SOCKET_SO_MARK.has_value()) {
    // The option isn't supported on this platform. Just skip the test.
    return;
  }

  auto filter = makeMarkingFilter(0);
  Network::Socket::OptionsSharedPtr options;
  setAddressToReturn("tcp://1.2.3.4:80");
  EXPECT_CALL(callbacks_, addUpstreamSocketOptions(_)).WillOnce(SaveArg<0>(&options));

  filter->decodeHeaders(headers_, false);

  auto mark_option = findOptionDetails(*options, ENVOY_SOCKET_SO_MARK,
                                       envoy::api::v2::core::SocketOption::STATE_PREBIND);

  ASSERT_FALSE(mark_option.has_value());
}

TEST_F(OriginalSrcTest, decodeDataDoesNothing) {
  StrictMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = makeFilterWithCallbacks(callbacks);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(buffer_, true));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(buffer_, false));
}

TEST_F(OriginalSrcTest, decodeTrailersDoesNothing) {
  StrictMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
  auto filter = makeFilterWithCallbacks(callbacks);

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->decodeTrailers(headers_));

  // make sure the headers aren't changed at all by comparing them to the default.
  EXPECT_EQ(headers_, Http::TestHeaderMapImpl());
}
} // namespace
} // namespace OriginalSrc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
