#include "test/mocks/request_info/mocks.h"

#include "common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace RequestInfo {

MockRequestInfo::MockRequestInfo()
    : downstream_local_address_(new Network::Address::Ipv4Instance("127.0.0.2")),
      downstream_remote_address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&host_));
  ON_CALL(*this, startTime()).WillByDefault(ReturnRef(start_time_));
  ON_CALL(*this, startTimeMonotonic()).WillByDefault(ReturnRef(start_time_monotonic_));
  ON_CALL(*this, lastDownstreamRxByteReceived()).WillByDefault(ReturnRef(last_rx_byte_received_));
  ON_CALL(*this, firstUpstreamTxByteSent()).WillByDefault(ReturnRef(first_upstream_tx_byte_sent_));
  ON_CALL(*this, lastUpstreamTxByteSent()).WillByDefault(ReturnRef(last_upstream_tx_byte_sent_));
  ON_CALL(*this, firstUpstreamRxByteReceived())
      .WillByDefault(ReturnRef(first_upstream_rx_byte_received_));
  ON_CALL(*this, lastUpstreamRxByteReceived())
      .WillByDefault(ReturnRef(last_upstream_rx_byte_received_));
  ON_CALL(*this, firstDownstreamTxByteSent())
      .WillByDefault(ReturnRef(first_downstream_tx_byte_sent_));
  ON_CALL(*this, lastDownstreamTxByteSent())
      .WillByDefault(ReturnRef(last_downstream_tx_byte_sent_));
  ON_CALL(*this, finalTimeMonotonic()).WillByDefault(ReturnRef(end_time_));
  ON_CALL(*this, upstreamLocalAddress()).WillByDefault(ReturnRef(upstream_local_address_));
  ON_CALL(*this, downstreamLocalAddress()).WillByDefault(ReturnRef(downstream_local_address_));
  ON_CALL(*this, downstreamRemoteAddress()).WillByDefault(ReturnRef(downstream_remote_address_));
  ON_CALL(*this, protocol()).WillByDefault(ReturnRef(protocol_));
  ON_CALL(*this, responseCode()).WillByDefault(ReturnRef(response_code_));
  ON_CALL(*this, bytesReceived()).WillByDefault(ReturnPointee(&bytes_received_));
  ON_CALL(*this, bytesSent()).WillByDefault(ReturnPointee(&bytes_sent_));
}

MockRequestInfo::~MockRequestInfo() {}

} // namespace RequestInfo
} // namespace Envoy
