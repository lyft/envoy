#include "extensions/filters/http/dynamo/dynamo_stats.h"

#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/stats/symbol_table_impl.h"

#include "extensions/filters/http/dynamo/dynamo_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

DynamoStats::DynamoStats(Stats::Scope& scope, const std::string& prefix)
    : scope_(scope), stat_name_set_(scope.symbolTable()),
      prefix_(stat_name_set_.add(prefix + "dynamodb")),
      batch_failure_unprocessed_keys_(stat_name_set_.add("BatchFailureUnprocessedKeys")),
      capacity_(stat_name_set_.add("capacity")),
      empty_response_body_(stat_name_set_.add("empty_response_body")),
      error_(stat_name_set_.add("error")),
      invalid_req_body_(stat_name_set_.add("invalid_req_body")),
      invalid_resp_body_(stat_name_set_.add("invalid_resp_body")),
      multiple_tables_(stat_name_set_.add("multiple_tables")),
      no_table_(stat_name_set_.add("no_table")),
      operation_missing_(stat_name_set_.add("operation_missing")),
      table_(stat_name_set_.add("table")), table_missing_(stat_name_set_.add("table_missing")),
      upstream_rq_time_(stat_name_set_.add("upstream_rq_time")),
      upstream_rq_total_(stat_name_set_.add("upstream_rq_total")) {
  upstream_rq_total_groups_[0] = stat_name_set_.add("upstream_rq_total_unknown");
  upstream_rq_time_groups_[0] = stat_name_set_.add("upstream_rq_time_unknown");
  for (size_t i = 1; i < DynamoStats::NumGroupEntries; ++i) {
    upstream_rq_total_groups_[i] = stat_name_set_.add(fmt::format("upstream_rq_total_{}xx", i));
    upstream_rq_time_groups_[i] = stat_name_set_.add(fmt::format("upstream_rq_time_{}xx", i));
  }
  RequestParser::forEachStatString(
      [this](const std::string& str) { stat_name_set_.rememberBuiltin(str); });
}

Stats::SymbolTable::StoragePtr DynamoStats::addPrefix(const std::vector<Stats::StatName>& names) {
  std::vector<Stats::StatName> names_with_prefix{prefix_};
  names_with_prefix.reserve(names.end() - names.begin());
  names_with_prefix.insert(names_with_prefix.end(), names.begin(), names.end());
  return scope_.symbolTable().join(names_with_prefix);
}

Stats::Counter& DynamoStats::counter(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Histogram& DynamoStats::histogram(const std::vector<Stats::StatName>& names) {
  const Stats::SymbolTable::StoragePtr stat_name_storage = addPrefix(names);
  return scope_.histogramFromStatName(Stats::StatName(stat_name_storage.get()));
}

Stats::Counter& DynamoStats::buildPartitionStatCounter(const std::string& table_name,
                                                       const std::string& operation,
                                                       const std::string& partition_id) {
  // Use the last 7 characters of the partition id.
  absl::string_view id_last_7 = absl::string_view(partition_id).substr(partition_id.size() - 7);
  const Stats::SymbolTable::StoragePtr stat_name_storage =
      addPrefix({table_, getStatName(table_name), capacity_, getStatName(operation),
                 getStatName(absl::StrCat("__partition_id=", id_last_7))});
  return scope_.counterFromStatName(Stats::StatName(stat_name_storage.get()));
}

size_t DynamoStats::groupIndex(uint64_t status) {
  size_t index = status / 100;
  if (index >= NumGroupEntries) {
    index = 0; // status-code 600 or higher is unknown.
  }
  return index;
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
