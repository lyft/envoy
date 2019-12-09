#include "extensions/tracers/xray/daemon_broker.h"

#include "envoy/network/address.h"

#include "common/network/utility.h"

#include "source/extensions/tracers/xray/daemon.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
// creates a header JSON for X-Ray daemon.
// For example:
// { "format": "json", "version": 1}
std::string createHeader(const std::string& format, uint32_t version) {
  envoy::tracers::xray::daemon::Header header;
  header.set_format(format);
  header.set_version(version);

  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  std::string json;
  const auto status = Protobuf::util::MessageToJsonString(header, &json, json_options);
  if (!status.ok()) {
    throw new EnvoyException("Failed to serialize X-Ray header to JSON.");
  }
  return json;
}

} // namespace

DaemonBrokerImpl::DaemonBrokerImpl(const std::string& daemon_endpoint) {
  const Network::Address::InstanceConstSharedPtr address =
      Network::Utility::parseInternetAddressAndPort(daemon_endpoint, false /*v6only*/);
  io_handle_ = address->socket(Network::Address::SocketType::Datagram);
  RELEASE_ASSERT(
      io_handle_->fd() != -1,
      absl::StrCat("Failed to acquire UDP socket to X-Ray daemon at - ", daemon_endpoint));
}

void DaemonBrokerImpl::send(const std::string& data) const {
  constexpr auto version = 1;
  constexpr auto format = "json";
  const std::string payload = absl::StrCat(createHeader(format, version), "\n", data);
  ::send(io_handle_->fd(), payload.c_str(), payload.size(), MSG_DONTWAIT);
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
