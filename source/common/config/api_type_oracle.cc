#include "common/config/api_type_oracle.h"

#include "udpa/annotations/versioning.pb.h"

namespace Envoy {
namespace Config {

// To exclude rejecting below v2 protos.
using Exclude_v2_protosSet = std::set<absl::string_view>;
static const Exclude_v2_protosSet& exclude_v2_protosSet() {
  CONSTRUCT_ON_FIRST_USE(
      Exclude_v2_protosSet,
      {"envoy.config.health_checker.redis.v2", "envoy.config.filter.thrift.router.v2alpha1",
       "envoy.config.resource_monitor.fixed_heap.v2alpha",
       "envoy.config.resource_monitor.injected_resource.v2alpha",
       "envoy.config.retry.omit_canary_hosts.v2", "envoy.config.retry.previous_hosts.v2"});
}

const Protobuf::Descriptor*
ApiTypeOracle::getEarlierVersionDescriptor(const std::string& message_type) {
  const auto previous_message_string = getEarlierVersionMessageTypeName(message_type);
  if (previous_message_string != absl::nullopt) {
    const Protobuf::Descriptor* earlier_desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            previous_message_string.value());
    auto itr = exclude_v2_protosSet().find(earlier_desc->full_name());
    const Protobuf::Descriptor* earlier_desc_exclude_v2_protos;
    if (itr == exclude_v2_protosSet().end()) {
      earlier_desc_exclude_v2_protos = earlier_desc;
    }
    return earlier_desc_exclude_v2_protos;
  } else {
    return nullptr;
  }
}

const absl::optional<std::string>
ApiTypeOracle::getEarlierVersionMessageTypeName(const std::string& message_type) {
  // Determine if there is an earlier API version for message_type.
  const Protobuf::Descriptor* desc =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(std::string{message_type});
  if (desc == nullptr) {
    return absl::nullopt;
  }
  if (desc->options().HasExtension(udpa::annotations::versioning)) {
    return desc->options().GetExtension(udpa::annotations::versioning).previous_message_type();
  }
  return absl::nullopt;
}

const absl::optional<std::string> ApiTypeOracle::getEarlierTypeUrl(const std::string& type_url) {
  const std::string type{TypeUtil::typeUrlToDescriptorFullName(type_url)};
  absl::optional<std::string> old_type = ApiTypeOracle::getEarlierVersionMessageTypeName(type);
  if (old_type.has_value()) {
    return TypeUtil::descriptorFullNameToTypeUrl(old_type.value());
  }
  return {};
}

} // namespace Config
} // namespace Envoy
