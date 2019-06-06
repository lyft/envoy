#include "common/config/watch_map.h"

namespace Envoy {
namespace Config {

WatchMap::Token WatchMap::addWatch(SubscriptionCallbacks& callbacks) {
  WatchMap::Token next_watch = next_watch_++;
  watches_.emplace(next_watch, WatchMap::Watch(callbacks));
  wildcard_watches_.insert(next_watch);
  return next_watch;
}

bool WatchMap::removeWatch(WatchMap::Token token) {
  watches_.erase(token);
  wildcard_watches_.erase(token); // may or may not be in there, but we want it gone.
  return watches_.empty();
}

std::pair<std::set<std::string>, std::set<std::string>>
WatchMap::updateWatchInterest(WatchMap::Token token,
                              const std::set<std::string>& update_to_these_names) {
  auto watches_entry = watches_.find(token);
  if (watches_entry == watches_.end()) {
    ENVOY_LOG(error, "updateWatchInterest() called on nonexistent token!");
    return std::make_pair(std::set<std::string>(), std::set<std::string>());
  }
  if (update_to_these_names.empty()) {
    wildcard_watches_.insert(token);
  } else {
    wildcard_watches_.erase(token);
  }

  auto& watch = watches_entry->second;

  std::vector<std::string> newly_added_to_watch;
  std::set_difference(update_to_these_names.begin(), update_to_these_names.end(),
                      watch.resource_names_.begin(), watch.resource_names_.end(),
                      std::inserter(newly_added_to_watch, newly_added_to_watch.begin()));

  std::vector<std::string> newly_removed_from_watch;
  std::set_difference(watch.resource_names_.begin(), watch.resource_names_.end(),
                      update_to_these_names.begin(), update_to_these_names.end(),
                      std::inserter(newly_removed_from_watch, newly_removed_from_watch.begin()));

  watch.resource_names_ = update_to_these_names;

  return std::make_pair(findAdditions(newly_added_to_watch, token),
                        findRemovals(newly_removed_from_watch, token));
}

absl::flat_hash_set<WatchMap::Token>
WatchMap::tokensInterestedIn(const std::string& resource_name) {
  // Note that std::set_union needs sorted sets. Better to do it ourselves with insert().
  absl::flat_hash_set<WatchMap::Token> ret = wildcard_watches_;
  auto watches_interested = watch_interest_.find(resource_name);
  if (watches_interested != watch_interest_.end()) {
    for (const auto& watch : watches_interested->second) {
      ret.insert(watch);
    }
  }
  return ret;
}

void WatchMap::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                              const std::string& version_info) {
  if (watches_.empty()) {
    ENVOY_LOG(warn, "WatchMap::onConfigUpdate: there are no watches!");
    return;
  }
  SubscriptionCallbacks& name_getter = watches_.begin()->second.callbacks_;

  // Build a map from watches, to the set of updated resources that each watch cares about. Each
  // entry in the map is then a nice little bundle that can be fed directly into the individual
  // onConfigUpdate()s.
  absl::flat_hash_map<WatchMap::Token, Protobuf::RepeatedPtrField<ProtobufWkt::Any>>
      per_watch_updates;
  for (const auto& r : resources) {
    const absl::flat_hash_set<WatchMap::Token>& interested_in_r =
        tokensInterestedIn(name_getter.resourceName(r));
    for (const auto& interested_token : interested_in_r) {
      per_watch_updates[interested_token].Add()->CopyFrom(r);
    }
  }

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (auto& watch : watches_) {
    auto this_watch_updates = per_watch_updates.find(watch.first);
    if (this_watch_updates == per_watch_updates.end()) {
      // This update included no resources this watch cares about - so we do an empty
      // onConfigUpdate(), to notify the watch that its resources - if they existed before this -
      // were dropped.
      watch.second.callbacks_.onConfigUpdate({}, version_info);
    } else {
      watch.second.callbacks_.onConfigUpdate(this_watch_updates->second, version_info);
    }
  }
}

void WatchMap::tryDeliverConfigUpdate(
    WatchMap::Token token,
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  auto entry = watches_.find(token);
  if (entry == watches_.end()) {
    ENVOY_LOG(error, "A token referred to by watch_interest_ is not present in watches_!");
    return;
  }
  entry->second.callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
}

void WatchMap::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  if (watches_.empty()) {
    ENVOY_LOG(warn, "WatchMap::onConfigUpdate: there are no watches!");
    return;
  }

  // Build a pair of maps: from watches, to the set of resources {added,removed} that each watch
  // cares about. Each entry in the map-pair is then a nice little bundle that can be fed directly
  // into the individual onConfigUpdate()s.
  absl::flat_hash_map<WatchMap::Token, Protobuf::RepeatedPtrField<envoy::api::v2::Resource>>
      per_watch_added;
  for (const auto& r : added_resources) {
    const absl::flat_hash_set<WatchMap::Token>& interested_in_r = tokensInterestedIn(r.name());
    for (const auto& interested_token : interested_in_r) {
      per_watch_added[interested_token].Add()->CopyFrom(r);
    }
  }
  absl::flat_hash_map<WatchMap::Token, Protobuf::RepeatedPtrField<std::string>> per_watch_removed;
  for (const auto& r : removed_resources) {
    const absl::flat_hash_set<WatchMap::Token>& interested_in_r = tokensInterestedIn(r);
    for (const auto& interested_token : interested_in_r) {
      *per_watch_removed[interested_token].Add() = r;
    }
  }

  // We just bundled up the updates into nice per-watch packages. Now, deliver them.
  for (const auto& added : per_watch_added) {
    const WatchMap::Token& cur_token = added.first;
    auto removed = per_watch_removed.find(cur_token);
    if (removed == per_watch_removed.end()) {
      // additions only, no removals
      tryDeliverConfigUpdate(cur_token, added.second, {}, system_version_info);
    } else {
      // both additions and removals
      tryDeliverConfigUpdate(cur_token, added.second, removed->second, system_version_info);
      // Drop the removals now, so the final removals-only pass won't use them.
      per_watch_removed.erase(removed);
    }
  }
  // Any removals-only updates will not have been picked up in the per_watch_added loop.
  for (const auto& removed : per_watch_removed) {
    tryDeliverConfigUpdate(removed.first, {}, removed.second, system_version_info);
  }
}

void WatchMap::onConfigUpdateFailed(const EnvoyException* e) {
  for (auto& watch : watches_) {
    watch.second.callbacks_.onConfigUpdateFailed(e);
  }
}

std::set<std::string> WatchMap::findAdditions(const std::vector<std::string>& newly_added_to_watch,
                                              WatchMap::Token token) {
  std::set<std::string> newly_added_to_subscription;
  for (const auto& name : newly_added_to_watch) {
    auto entry = watch_interest_.find(name);
    if (entry == watch_interest_.end()) {
      newly_added_to_subscription.insert(name);
      watch_interest_[name] = {token};
    } else {
      entry->second.insert(token);
    }
  }
  return newly_added_to_subscription;
}

std::set<std::string>
WatchMap::findRemovals(const std::vector<std::string>& newly_removed_from_watch,
                       WatchMap::Token token) {
  std::set<std::string> newly_removed_from_subscription;
  for (const auto& name : newly_removed_from_watch) {
    auto entry = watch_interest_.find(name);
    if (entry == watch_interest_.end()) {
      ENVOY_LOG(warn, "WatchMap: tried to remove a watch from untracked resource {}", name);
      continue;
    }

    entry->second.erase(token);
    if (entry->second.empty()) {
      watch_interest_.erase(entry);
      newly_removed_from_subscription.insert(name);
    }
  }
  return newly_removed_from_subscription;
}

} // namespace Config
} // namespace Envoy
