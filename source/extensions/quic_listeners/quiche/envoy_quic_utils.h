#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// TODO(danzh): this is called on each write. Consider to return an address instance on the stack if
// the heap allocation is too expensive.
Network::Address::InstanceConstSharedPtr
quicAddressToEnvoyAddressInstance(const quic::QuicSocketAddress& quic_address);

quic::QuicSocketAddress envoyAddressInstanceToQuicSocketAddress(
    const Network::Address::InstanceConstSharedPtr& envoy_address);

Http::HeaderMapImplPtr quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list);

Http::HeaderMapImplPtr spdyHeaderBlockToEnvoyHeaders(const spdy::SpdyHeaderBlock& header_block);

spdy::SpdyHeaderBlock envoyHeadersToSpdyHeaderBlock(const Http::HeaderMap& headers);

Http::StreamResetReason quicRstErrorToEnvoyResetReason(quic::QuicRstStreamErrorCode quic_rst);

} // namespace Quic
} // namespace Envoy
