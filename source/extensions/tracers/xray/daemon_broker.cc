#include "extensions/tracers/xray/daemon_broker.h"

#include "envoy/network/address.h"

#include "common/buffer/buffer_impl.h"
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
  ASSERT(status.ok());
  return json;
}

} // namespace

DaemonBrokerImpl::DaemonBrokerImpl(const std::string& daemon_endpoint) {
  address_ = Network::Utility::parseInternetAddressAndPort(daemon_endpoint, false /*v6only*/);
  io_handle_ = address_->socket(Network::Address::SocketType::Datagram);
}

void DaemonBrokerImpl::send(const std::string& data) const {
  auto& logger = Logger::Registry::getLog(Logger::Id::tracing);
  constexpr auto version = 1;
  constexpr auto format = "json";
  const std::string payload = absl::StrCat(createHeader(format, version), "\n", data);
  Buffer::RawSlice buf;
  buf.mem_ = const_cast<char*>(payload.data());
  buf.len_ = payload.length();
  const auto rc = Network::Utility::writeToSocket(*io_handle_, &buf, 1 /*num_slices*/,
                                                  nullptr /*local_ip*/, *address_);

  if (rc.rc_ != payload.length()) {
    // TODO(marcomagdy): report this in stats
    ENVOY_LOG_TO_LOGGER(logger, debug, "Failed to send trace payload to the X-Ray daemon.");
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
