#include "extensions/filters/network/common/redis/redis_command_stats.h"

#include "extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

RedisCommandStats::RedisCommandStats(Stats::Scope& scope, const std::string& prefix, bool enableCommandCounts)
    : scope_(scope), stat_name_set_(scope.symbolTable()),
      prefix_(stat_name_set_.add(prefix)),
      upstream_rq_time_(stat_name_set_.add("upstream_rq_time")) {
  
  // Note: Even if enableCommandCounts is disabled, we track the upstrea_rq_time.
  if (enableCommandCounts) {
    // Create StatName for each Redis command. Note that we don't include Auth or Ping.
    for (const std::string& command :
        Extensions::NetworkFilters::Common::Redis::SupportedCommands::simpleCommands()) {
      stat_name_set_.add(command);
    }
    for (const std::string& command :
        Extensions::NetworkFilters::Common::Redis::SupportedCommands::evalCommands()) {
      stat_name_set_.add(command);
    }
    for (const std::string& command : Extensions::NetworkFilters::Common::Redis::SupportedCommands::
            hashMultipleSumResultCommands()) {
      stat_name_set_.add(command);
    }
    stat_name_set_.add(Extensions::NetworkFilters::Common::Redis::SupportedCommands::mget());
    stat_name_set_.add(Extensions::NetworkFilters::Common::Redis::SupportedCommands::mset());
  }
}

Stats::Counter& RedisCommandStats::counter(std::string name) {
  return scope_.counterFromStatName(stat_name_set_.getStatName(name));
}

Stats::Histogram& RedisCommandStats::histogram(Stats::StatName statName) {
  return scope_.histogramFromStatName(statName);
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy