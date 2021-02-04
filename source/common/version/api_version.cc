#include "common/version/api_version.h"

#include <regex>
#include <string>

#include "common/common/fmt.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {

std::string
ApiVersionInfo::apiVersionToString(const envoy::config::core::v3::ApiVersionNumber& version) {
  return fmt::format("{}.{}.{}", version.version().major_number(), version.version().minor_number(),
                     version.version().patch());
}

const envoy::config::core::v3::ApiVersionNumber& ApiVersionInfo::apiVersion() {
  static const auto* result =
      new envoy::config::core::v3::ApiVersionNumber(makeApiVersion(API_VERSION_NUMBER));
  return *result;
}

const envoy::config::core::v3::ApiVersionNumber& ApiVersionInfo::oldestApiVersion() {
  static const auto* result =
      new envoy::config::core::v3::ApiVersionNumber(computeOldestApiVersion(apiVersion()));
  return *result;
}

envoy::config::core::v3::ApiVersionNumber ApiVersionInfo::makeApiVersion(const char* version) {
  envoy::config::core::v3::ApiVersionNumber result;
  // Split API_VERSION_NUMBER into version
  std::regex ver_regex("([\\d]+)\\.([\\d]+)\\.([\\d]+)");
  // Match indexes, given the regex above
  constexpr std::cmatch::size_type major = 1;
  constexpr std::cmatch::size_type minor = 2;
  constexpr std::cmatch::size_type patch = 3;
  std::cmatch match;
  if (std::regex_match(version, match, ver_regex)) {
    uint32_t value = 0;
    if (absl::SimpleAtoi(match.str(major), &value)) {
      result.mutable_version()->set_major_number(value);
    }
    if (absl::SimpleAtoi(match.str(minor), &value)) {
      result.mutable_version()->set_minor_number(value);
    }
    if (absl::SimpleAtoi(match.str(patch), &value)) {
      result.mutable_version()->set_patch(value);
    }
  }
  return result;
}

envoy::config::core::v3::ApiVersionNumber ApiVersionInfo::computeOldestApiVersion(
    const envoy::config::core::v3::ApiVersionNumber& latest_version) {
  envoy::config::core::v3::ApiVersionNumber result;
  // The oldest API version that is supported by the client is up to 2 minor
  // versions before the latest version. Note that the major number is always
  // the same as the latest version, and the patch number is always 0. This
  // implies that the minor number is at least 0, and the oldest api version
  // cannot be set to a previous major number.
  result.mutable_version()->set_major_number(latest_version.version().major_number());
  result.mutable_version()->set_minor_number(std::max(
      static_cast<int64_t>(latest_version.version().minor_number()) - 2, static_cast<int64_t>(0)));
  result.mutable_version()->set_patch(0);
  return result;
}
} // namespace Envoy
