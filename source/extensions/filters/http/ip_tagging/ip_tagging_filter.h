#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/filesystem/watcher.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "common/common/thread.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"
#include "common/protobuf/utility.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

using IpTagFileProto = envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTagFile;
using IPTagsProto =
    Protobuf::RepeatedPtrField<::envoy::extensions::filters::http::ip_tagging::v3::IPTagging_IPTag>;
using TriePtr = std::unique_ptr<Network::LcTrie::LcTrie<std::string>>;
using StatsTrieSet = std::pair<Stats::StatNameSetPtr, TriePtr>;

/**
 * Coordinates with the Filesystem::Watcher and when that reports a change, load up
 * the change and updates it's internal settings.
 */
class TagSetWatcher {
private:
  class Registry;

public:
  static std::shared_ptr<TagSetWatcher>
  create(Server::Configuration::FactoryContext& factory_context, std::string filename);

  TagSetWatcher(Server::Configuration::FactoryContext& factory_context,
                Event::Dispatcher& dispatcher, Api::Api& api, std::string filename,
                Stats::Scope& scope);

  TagSetWatcher(Server::Configuration::FactoryContext& factory_context, std::string filename)
      : TagSetWatcher(factory_context, factory_context.dispatcher(), factory_context.api(),
                      std::move(filename)) {}

  ~TagSetWatcher();

  const Network::LcTrie::LcTrie<std::string>& get() const { return *stats_trie_set_.second; }
  const std::string& filename() { return filename_; }

private:
  Envoy::ProtobufMessage::ValidationVisitor& protoValidator() const {
    return factory_context_.messageValidationVisitor();
  }

  void maybeUpdate_(bool force = false);
  void update_(absl::string_view content, std::uint64_t hash);

  std::unique_ptr<Network::LcTrie::LcTrie<std::string>>
  fileContentsAsTagSet_(absl::string_view contents) const;

  IpTagFileProto protoFromFileContents_(absl::string_view contents) const;

  Stats::Scope& scope_;
  Api::Api& api_;
  std::string filename_;
  std::string extension_;
  std::uint64_t content_hash_ = 0;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Server::Configuration::FactoryContext& factory_context_;
  Registry* registry_ = nullptr; // Set by registry.
  // std::unique_ptr<Network::LcTrie::LcTrie<std::string>> trie_;
  StatsTrieSet stats_trie_set_;

protected:
  bool yaml;
};

/**
 * The purpose of the registry is to create a single watcher for a file.
 */
class TagSetWatcher::Registry {
private:
  using map_type =
      absl::flat_hash_map<std::string, std::weak_ptr<TagSetWatcher>, absl::Hash<absl::string_view>>;

public:
  std::shared_ptr<TagSetWatcher> getOrCreate(Server::Configuration::FactoryContext& factory_context,
                                             std::string filename);

  void remove(TagSetWatcher& watcher) noexcept;

  static Registry& singleton();

private:
  mutable Thread::MutexBasicLockable mtx_;
  map_type map_ ABSL_GUARDED_BY(mtx_);
};

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Configuration for the HTTP IP Tagging filter.
 */
class IpTaggingFilterConfig {
public:
  IpTaggingFilterConfig(const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
                        const std::string& stat_prefix, Stats::Scope& scope,
                        Runtime::Loader& runtime,
                        Envoy::Server::Configuration::FactoryContext& factory_context);

  Runtime::Loader& runtime() { return runtime_; }
  FilterRequestType requestType() const { return request_type_; }

  Stats::StatNameSetPtr initializeStatsPtr(Stats::Scope& scope, Stats::StatNameSetPtr);

  const Network::LcTrie::LcTrie<std::string>& trie() const {
    if (watcher_ != nullptr) {
      return watcher_->get();
    } else {
      return *stats_trie_set_.second;
    }
  }

  void incHit(absl::string_view tag) {
    incCounter(stats_trie_set_.first->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_));
  }
  void incNoHit() { incCounter(no_hit_); }
  void incTotal() { incCounter(total_); }

  // void setStatsData(Stats::StatNameSetPtr);

  static StatsTrieSet IpTaggingFilterSetTagData(const IPTagsProto& ip_tags);

private:
  static FilterRequestType requestTypeEnum(
      envoy::extensions::filters::http::ip_tagging::v3::IPTagging::RequestType request_type) {
    switch (request_type) {
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::BOTH:
      return FilterRequestType::BOTH;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::INTERNAL:
      return FilterRequestType::INTERNAL;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::EXTERNAL:
      return FilterRequestType::EXTERNAL;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  void incCounter(Stats::StatName name);

  const FilterRequestType request_type_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName no_hit_;
  const Stats::StatName total_;
  const Stats::StatName unknown_tag_;
  std::shared_ptr<const TagSetWatcher> watcher_;
  StatsTrieSet stats_trie_set_;
  // Stats::StatNameSetPtr stats_name_set_;
  // std::unique_ptr<Network::LcTrie::LcTrie<std::string>> trie_;
};

using IpTaggingFilterConfigSharedPtr = std::shared_ptr<IpTaggingFilterConfig>;

/**
 * A filter that gets all tags associated with a request's downstream remote address and
 * sets a header `x-envoy-ip-tags` with those values.
 */
class IpTaggingFilter : public Http::StreamDecoderFilter {
public:
  IpTaggingFilter(IpTaggingFilterConfigSharedPtr config);
  ~IpTaggingFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  IpTaggingFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
