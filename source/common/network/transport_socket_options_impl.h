#pragma once

#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

// A wrapper around another TransportSocketOptions that overrides the ALPN.
class AlpnDecoratingTransportSocketOptions : public TransportSocketOptions {
public:
  AlpnDecoratingTransportSocketOptions(std::vector<std::string>&& alpn,
                                       TransportSocketOptionsSharedPtr inner_options)
      : alpn_list_(std::move(alpn)), inner_options_(std::move(inner_options)) {}
  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return inner_options_->serverNameOverride();
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return inner_options_->verifySubjectAltNameListOverride();
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return alpn_list_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const std::vector<std::string> alpn_list_;
  const TransportSocketOptionsSharedPtr inner_options_;
};

class TransportSocketOptionsImpl : public TransportSocketOptions {
public:
  TransportSocketOptionsImpl(absl::string_view override_server_name = "",
                             std::vector<std::string>&& override_verify_san_list = {},
                             std::vector<std::string>&& override_alpn = {})
      : override_server_name_(override_server_name.empty()
                                  ? absl::nullopt
                                  : absl::optional<std::string>(override_server_name)),
        override_verify_san_list_{std::move(override_verify_san_list)},
        override_alpn_list_{std::move(override_alpn)} {}

  // Network::TransportSocketOptions
  const absl::optional<std::string>& serverNameOverride() const override {
    return override_server_name_;
  }
  const std::vector<std::string>& verifySubjectAltNameListOverride() const override {
    return override_verify_san_list_;
  }
  const std::vector<std::string>& applicationProtocolListOverride() const override {
    return override_alpn_list_;
  }
  void hashKey(std::vector<uint8_t>& key) const override;

private:
  const absl::optional<std::string> override_server_name_;
  const std::vector<std::string> override_verify_san_list_;
  const std::vector<std::string> override_alpn_list_;
};

class TransportSocketOptionsUtility {
public:
  /**
   * Construct TransportSocketOptions from StreamInfo::FilterState, using UpstreamServerName
   * and ApplicationProtocols key in the filter state.
   * @returns TransportSocketOptionsSharedPtr a shared pointer to the transport socket options,
   * nullptr if nothing is in the filter state.
   */
  static TransportSocketOptionsSharedPtr
  fromFilterState(const StreamInfo::FilterState& stream_info);
};

} // namespace Network
} // namespace Envoy
