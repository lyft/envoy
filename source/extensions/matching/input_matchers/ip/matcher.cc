#include "extensions/matching/input_matchers/ip/matcher.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

Matcher::Matcher(std::vector<Network::Address::CidrRange>&& ranges, absl::string_view stat_prefix,
                 Stats::Scope& stat_scope)
    : // We could put "false" instead of "true". What matters is that the IP
      // belongs to the trie. We could further optimize the storage of LcTrie in
      // this case by implementing an LcTrie<void> specialization that doesn't
      // store any associated data.
      trie_({{true, std::move(ranges)}}) {
  if (!stat_prefix.empty()) {
    stats_.emplace(generateStats(stat_prefix, stat_scope));
  }
}

MatcherStats Matcher::generateStats(absl::string_view prefix, Stats::Scope& scope) {
  return MatcherStats{IP_MATCHER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

bool Matcher::match(absl::optional<absl::string_view> input) {
  if (!input) {
    return false;
  }
  const absl::string_view ip_str = *input;
  if (ip_str.empty()) {
    return false;
  }
  const auto ip = Network::Utility::parseInternetAddressNoThrow(std::string{ip_str});
  if (!ip) {
    if (stats_) {
      stats_->ip_parsing_failed_.inc();
    }
    ENVOY_LOG(warn, "IP matcher: unable to parse address '{}'", ip_str);
    return false;
  }
  return !trie_.getData(ip).empty();
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
