#pragma once

#include "envoy/network/address.h"
#include "envoy/ssl/context_manager.h"

namespace Envoy {
namespace Ssl {

ClientContextPtr createClientSslContext(bool alpn, bool san, ContextManager* context_manager);

Network::Address::InstanceConstSharedPtr getSslAddress(Network::Address::IpVersion version,
                                                       int port);

} // Ssl
} // Envoy
