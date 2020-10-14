#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/http/header_utility.h"

#include "extensions/common/matcher/matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

class MatchWrapper {
public:
  explicit MatchWrapper(const envoy::config::common::matcher::v3::MatchPredicate match_config) {
    Extensions::Common::Matcher::buildMatcher(match_config, matchers_);
    status_.resize(matchers_.size());
  }

  Extensions::Common::Matcher::Matcher& rootMatcher() { return *matchers_[0]; }

  std::vector<Extensions::Common::Matcher::Matcher::MatchStatus> status_;

private:
  std::vector<Extensions::Common::Matcher::MatcherPtr> matchers_;
};

using MatchWrapperSharedPtr = std::shared_ptr<MatchWrapper>;

class MatchTreeFactoryCallbacks {
public:
  virtual ~MatchTreeFactoryCallbacks() = default;

  virtual void addPredicateMatcher(MatchWrapperSharedPtr matcher) PURE;
};

class KeyNamespaceMapper {
public:
  virtual ~KeyNamespaceMapper() = default;
  virtual void forEachValue(absl::string_view ns, absl::string_view key,
                            const MatchingData& matching_data,
                            std::function<void(absl::string_view)> value_cb) PURE;
};

using KeyNamespaceMapperSharedPtr = std::shared_ptr<KeyNamespaceMapper>;

class MultimapMatcher : public MatchTree {
public:
  MultimapMatcher(std::string key, std::string ns, KeyNamespaceMapperSharedPtr namespace_mapper,
                  MatchTreeSharedPtr no_match_tree)
      : key_(key), namespace_(ns), key_namespace_mapper_(std::move(namespace_mapper)),
        no_match_tree_(std::move(no_match_tree)) {}

  MatchResult match(const MatchingData& data) override {
    bool first_value_evaluated = false;
    absl::optional<std::reference_wrapper<MatchTree>> selected_subtree = absl::nullopt;
    key_namespace_mapper_->forEachValue(namespace_, key_, data, [&](auto value) {
      if (first_value_evaluated) {
        return;
      }
      // TODO(snowp): Only match on the first header for now.
      first_value_evaluated = true;

      const auto itr = children_.find(value);
      if (itr != children_.end()) {
        selected_subtree = absl::make_optional(std::ref(*itr->second));
      }
    });

    if (selected_subtree) {
      return selected_subtree->get().match(data);
    }

    if (no_match_tree_) {
      return no_match_tree_->match(data);
    }

    return {true, absl::nullopt};
  }

  void addChild(std::string value, MatchTreeSharedPtr&& subtree) {
    children_[value] = std::move(subtree);
  }

private:
  const std::string key_;
  const std::string namespace_;
  KeyNamespaceMapperSharedPtr key_namespace_mapper_;
  absl::flat_hash_map<std::string, MatchTreeSharedPtr> children_;
  MatchTreeSharedPtr no_match_tree_;
};

class AlwaysSkipMatcher : public MatchTree {
public:
  MatchResult match(const MatchingData&) override { return {true, MatchAction::skip()}; }
};

class AlwaysCallbackMatcher : public MatchTree {
public:
  explicit AlwaysCallbackMatcher(std::string callback) : callback_(callback) {}

  MatchResult match(const MatchingData&) override {
    return {true, MatchAction::callback(callback_)};
  }

private:
  const std::string callback_;
};

class LeafMatcher {
public:
  virtual ~LeafMatcher() = default;

  virtual absl::optional<bool> match(const MatchingData& data) PURE;
};

using LeafMatcherPtr = std::unique_ptr<LeafMatcher>;

class HttpPredicateMatcher : public LeafMatcher {
public:
  explicit HttpPredicateMatcher(MatchWrapperSharedPtr matcher) : matcher_(std::move(matcher)) {}

  absl::optional<bool> match(const MatchingData&) override {
    const auto& status = matcher_->rootMatcher().matchStatus(matcher_->status_);

    if (status.might_change_status_) {
      return absl::nullopt;
    }

    return status.matches_;
  }

  MatchWrapperSharedPtr matcher_;
};

class LeafNode : public MatchTree {
public:
  LeafNode(absl::optional<MatchAction> no_match_action) : no_match_action_(no_match_action) {}

  MatchResult match(const MatchingData& matching_data) override {
    for (const auto& matcher : matchers_) {
      const auto maybe_match = matcher.first->match(matching_data);
      // One of the matchers don't have enough information, delay.
      if (!maybe_match) {
        return {false, {}};
      }

      if (*maybe_match) {
        return {true, matcher.second};
      }
    }

    return {true, no_match_action_};
  }

  void addMatcher(LeafMatcherPtr&& matcher, MatchAction action) {
    matchers_.push_back({std::move(matcher), action});
  }

private:
  absl::optional<MatchAction> no_match_action_;
  std::vector<std::pair<LeafMatcherPtr, MatchAction>> matchers_;
};

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 */
class MatchTreeFactory {
public:
  static MatchTreeSharedPtr create(const envoy::config::common::matcher::v3::MatchTree& config,
                                   KeyNamespaceMapperSharedPtr key_namespace_mapper,
                                   MatchTreeFactoryCallbacks& callbacks);

private:
  static MatchTreeSharedPtr
  createLinearMatcher(const envoy::config::common::matcher::v3::MatchTree::MatchLeaf& config,
                      KeyNamespaceMapperSharedPtr, MatchTreeFactoryCallbacks& callbacks);

  static MatchTreeSharedPtr createSublinerMatcher(
      const envoy::config::common::matcher::v3::MatchTree::SublinearMatcher& matcher,
      KeyNamespaceMapperSharedPtr key_namespace_mapper, MatchTreeFactoryCallbacks& callbacks);
};
} // namespace Envoy
