#pragma once

#include <functional>
#include <map>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/non_copyable.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * ConfigTracker is used by the `/config_dump` admin endpoint to manage storage of config-providing
 * callbacks with weak ownership semantics. Callbacks added to ConfigTracker only live as long as
 * the returned EntryOwner object (or ConfigTracker itself, if shorter). Keys should be descriptors
 * of the configs provided by the corresponding callback. They must be unique.
 * ConfigTracker is *not* threadsafe.
 */
class ConfigTracker {
public:
  typedef std::function<ProtobufTypes::MessagePtr()> Cb;
  typedef std::map<std::string, Cb> CbsMap;
  typedef std::map<std::string, ProtobufTypes::MessageSharedPtr> ManagedConfigMap;

  /**
   * EntryOwner supplies RAII semantics for entries in the map.
   * The entry is not removed until the EntryOwner or the ConfigTracker itself is destroyed,
   * whichever happens first. When you add() an entry, you must hold onto the returned
   * owner object for as long as you want the entry to stay in the map.
   */
  class EntryOwner {
  public:
    virtual ~EntryOwner() {}

  protected:
    EntryOwner(){}; // A sly way to make this class "abstract."
  };
  typedef std::unique_ptr<EntryOwner> EntryOwnerPtr;

  virtual ~ConfigTracker(){};

  /**
   * @return const CbsMap& The map of string keys to tracked callbacks.
   */
  virtual const CbsMap& getCallbacksMap() const PURE;

  /**
   * Add a new callback to the map under the given key
   * @param key the map key for the new callback.
   * @param cb the callback to add. *must not* return nullptr.
   * @return EntryOwnerPtr the new entry's owner object. nullptr if the key is already present.
   */
  virtual EntryOwnerPtr add(const std::string& key, Cb cb) PURE;

  /**
   * Add or update a managed config to the config tracker under the given key
   * @param key the map key for the configuration.
   * @param message the message to be managed by config tracker.
   */
  virtual void addOrUpdateManagedConfig(const std::string& key,
                                        ProtobufTypes::MessageSharedPtr message) PURE;

  /**
   * Returns config managed by config tracker under the given key
   * @param key the map key for the configuration.
   * @return ProtobufTypes::MessageSharedPtr the message stored under the key.
   */
  virtual ProtobufTypes::MessageSharedPtr getManagedConfig(const std::string& key) const PURE;

  /**
   * Returns manged config map by config tracker.
   * @return ManagedConfigMap the message map managed by config tracker.
   */
  virtual const ManagedConfigMap& getManagedConfigMap() const PURE;
};

} // namespace Server
} // namespace Envoy
