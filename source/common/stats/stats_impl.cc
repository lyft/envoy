#include "common/stats/stats_impl.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <list>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/utility.h"
#include "common/config/well_known_names.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Stats {

namespace {

// Round val up to the next multiple of the natural alignment.
// Note: this implementation only works because 8 is a power of 2.
size_t roundUpMultipleNaturalAlignment(size_t val) {
  const size_t multiple = alignof(RawStatData);
  static_assert(multiple == 1 || multiple == 2 || multiple == 4 || multiple == 8 || multiple == 16,
                "multiple must be a power of 2 for this algorithm to work");
  return (val + multiple - 1) & ~(multiple - 1);
}

} // namespace

size_t RawStatData::size() {
  // Normally the compiler would do this, but because name_ is a flexible-array-length
  // element, the compiler can't. RawStatData is put into an array in HotRestartImpl, so
  // it's important that each element starts on the required alignment for the type.
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + nameSize());
}

size_t& RawStatData::initializeAndGetMutableMaxObjNameLength(size_t configured_size) {
  // Like CONSTRUCT_ON_FIRST_USE, but non-const so that the value can be changed by tests
  static size_t size = configured_size;
  return size;
}

void RawStatData::configure(Server::Options& options) {
  const size_t configured = options.maxObjNameLength();
  RELEASE_ASSERT(configured > 0);
  size_t max_obj_name_length = initializeAndGetMutableMaxObjNameLength(configured);

  // If this fails, it means that this function was called too late during
  // startup because things were already using this size before it was set.
  RELEASE_ASSERT(max_obj_name_length == configured);
}

void RawStatData::configureForTestsOnly(Server::Options& options) {
  const size_t configured = options.maxObjNameLength();
  initializeAndGetMutableMaxObjNameLength(configured) = configured;
}

std::string Utility::sanitizeStatsName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  return stats_name;
}

TagExtractorImpl::TagExtractorImpl(const std::string& name, const std::string& regex)
    : name_(name), prefix_(std::string(extractRegexPrefix(regex))),
      regex_(RegexUtil::parseRegex(regex)) {}

std::string TagExtractorImpl::extractRegexPrefix(absl::string_view regex) {
  std::string prefix;
  if (absl::StartsWith(regex, "^")) {
    for (absl::string_view::size_type i = 1; i < regex.size(); ++i) {
      if (!absl::ascii_isalnum(regex[i]) && (regex[i] != '_')) {
        if (i > 1) {
          bool last_char = i == regex.size() - 1;
          if ((!last_char && (regex[i] == '\\') && (regex[i + 1] == '.')) ||
              (last_char && (regex[i] == '$'))) {
            prefix.append(regex.data() + 1, i - 1);
          }
        }
        break;
      }
    }
  }
  return prefix;
}

TagExtractorPtr TagExtractorImpl::createTagExtractor(const std::string& name,
                                                     const std::string& regex) {

  if (name.empty()) {
    throw EnvoyException("tag_name cannot be empty");
  }

  if (regex.empty()) {
    throw EnvoyException(fmt::format(
        "No regex specified for tag specifier and no default regex for name: '{}'", name));
  }
  return TagExtractorPtr{new TagExtractorImpl(name, regex)};
}

bool TagExtractorImpl::extractTag(const std::string& stat_name, std::vector<Tag>& tags,
                                  IntervalSet<size_t>& remove_characters) const {

  std::smatch match;
  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (std::regex_search(stat_name, match, regex_) && match.size() > 1) {
    // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
    const auto& remove_subexpr = match[1];

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    const auto& value_subexpr = match.size() > 2 ? match[2] : remove_subexpr;

    tags.emplace_back();
    Tag& tag = tags.back();
    tag.name_ = name_;
    tag.value_ = value_subexpr.str();

    // Determines which characters to remove from stat_name to elide remove_subexpr.
    std::string::size_type start = remove_subexpr.first - stat_name.begin();
    std::string::size_type end = remove_subexpr.second - stat_name.begin();
    remove_characters.insert(start, end);
    return true;
  }
  return false;
}

RawStatData* HeapRawStatDataAllocator::alloc(const std::string& name) {
  // This must be zero-initialized
  RawStatData* data = static_cast<RawStatData*>(::calloc(RawStatData::size(), 1));
  data->initialize(name);
  return data;
}

TagProducerImpl::TagProducerImpl(const envoy::config::metrics::v2::StatsConfig& config)
    : TagProducerImpl() {
  // To check name conflict.
  reserveResources(config);
  std::unordered_set<std::string> names = addDefaultExtractors(config);

  for (const auto& tag_specifier : config.stats_tags()) {
    if (!names.emplace(tag_specifier.tag_name()).second) {
      throw EnvoyException(fmt::format("Tag name '{}' specified twice.", tag_specifier.tag_name()));
    }

    // If no tag value is found, fallback to default regex to keep backward compatibility.
    if (tag_specifier.tag_value_case() ==
            envoy::config::metrics::v2::TagSpecifier::TAG_VALUE_NOT_SET ||
        tag_specifier.tag_value_case() == envoy::config::metrics::v2::TagSpecifier::kRegex) {
      if (tag_specifier.regex().empty()) {
        addExtractorsMatching(tag_specifier.tag_name());
      } else {
        addExtractor(Stats::TagExtractorImpl::createTagExtractor(tag_specifier.tag_name(),
                                                                 tag_specifier.regex()));
      }
    } else if (tag_specifier.tag_value_case() ==
               envoy::config::metrics::v2::TagSpecifier::kFixedValue) {
      default_tags_.emplace_back(
          Stats::Tag{.name_ = tag_specifier.tag_name(), .value_ = tag_specifier.fixed_value()});
    }
  }
}

void TagProducerImpl::addExtractorsMatching(absl::string_view name) {
  int num_found = 0;
  for (const auto& desc : Config::TagNames::get().descriptorVec()) {
    if (desc.name == name) {
      addExtractor(Stats::TagExtractorImpl::createTagExtractor(desc.name, desc.regex));
      ++num_found;
    }
  }
  if (num_found == 0) {
    throw EnvoyException(fmt::format(
        "No regex specified for tag specifier and no default regex for name: '{}'", name));
  }
}

void TagProducerImpl::addExtractor(TagExtractorPtr extractor) {
  absl::string_view prefix = extractor->prefixToken();
  if (prefix.empty()) {
    tag_extractors_without_prefix_.emplace_back(std::move(extractor));
  } else {
    tag_extractor_prefix_map_[prefix].emplace_back(std::move(extractor));
  }
}

void TagProducerImpl::forEachExtractorMatching(
    const std::string& stat_name, std::function<void(const TagExtractorPtr&)> f) const {
  IntervalSetImpl<size_t> remove_characters;
  for (const TagExtractorPtr& tag_extractor : tag_extractors_without_prefix_) {
    f(tag_extractor);
  }
  std::string::size_type dot = stat_name.find('.');
  if (dot != std::string::npos) {
    absl::string_view token = absl::string_view(stat_name.data(), dot);
    auto p = tag_extractor_prefix_map_.find(token);
    if (p != tag_extractor_prefix_map_.end()) {
      for (const TagExtractorPtr& tag_extractor : p->second) {
        f(tag_extractor);
      }
    }
  }
}

std::string TagProducerImpl::produceTags(const std::string& stat_name,
                                         std::vector<Tag>& tags) const {
  tags.insert(tags.end(), default_tags_.begin(), default_tags_.end());
  IntervalSetImpl<size_t> remove_characters;
  forEachExtractorMatching(
      stat_name, [&remove_characters, &tags, stat_name](const TagExtractorPtr& tag_extractor) {
        tag_extractor->extractTag(stat_name, tags, remove_characters);
      });
  return StringUtil::removeCharacters(stat_name, remove_characters);
}

void TagProducerImpl::reserveResources(const envoy::config::metrics::v2::StatsConfig& config) {
  default_tags_.reserve(config.stats_tags().size());
}

std::unordered_set<std::string>
TagProducerImpl::addDefaultExtractors(const envoy::config::metrics::v2::StatsConfig& config) {
  std::unordered_set<std::string> names;
  if (!config.has_use_all_default_tags() || config.use_all_default_tags().value()) {
    for (const auto& desc : Config::TagNames::get().descriptorVec()) {
      names.emplace(desc.name);
      addExtractor(Stats::TagExtractorImpl::createTagExtractor(desc.name, desc.regex));
    }
  }
  return names;
}

void HeapRawStatDataAllocator::free(RawStatData& data) {
  // This allocator does not ever have concurrent access to the raw data.
  ASSERT(data.ref_count_ == 1);
  ::free(&data);
}

void RawStatData::initialize(absl::string_view key) {
  ASSERT(!initialized());
  ASSERT(key.size() <= maxNameLength());
  ASSERT(absl::string_view::npos == key.find(':'));
  ref_count_ = 1;

  // key is not necessarily nul-terminated, but we want to make sure name_ is.
  size_t xfer_size = std::min(nameSize() - 1, key.size());
  memcpy(name_, key.data(), xfer_size);
  name_[xfer_size] = '\0';
}

} // namespace Stats
} // namespace Envoy
