#pragma once

#include <string>
#include <vector>

#include "common/network/address_impl.h"

#include "api/base.pb.h"
#include "api/bootstrap.pb.h"
#include "api/cds.pb.h"

namespace Envoy {

class ConfigHelper {
public:
  // Set up basic config, using the specified IpVersion for all connections: listeners, upstream,
  // and admin connections.
  ConfigHelper(const Network::Address::IpVersion version);

  // Set the upstream ports.  The size of this vector must match the number of socket addresses
  // across all configured clusters.
  void setUpstreamPorts(std::vector<uint32_t> ports);

  // Set source_address in the bootstrap bind config.
  void setSourceAddress(std::string address_string);

  // Return the bootstrap configuration for dyamic edits.
  envoy::api::v2::Bootstrap& bootstrap() { return bootstrap_; }

private:
  // The bootstrap proto Envoy will start up with.
  envoy::api::v2::Bootstrap bootstrap_;
};

} // namespace Envoy
