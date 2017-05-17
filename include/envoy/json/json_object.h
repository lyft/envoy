#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Json {
class Object;

typedef std::shared_ptr<Object> ObjectSharedPtr;

// @return false if immediate exit from iteration required.
typedef std::function<bool(const std::string&, const Object&)> ObjectCallback;

/**
 * Exception thrown when a JSON error occurs.
 */
class Exception : public EnvoyException {
public:
  Exception(const std::string& message) : EnvoyException(message) {}
};

/**
 * Wraps an individual JSON node.
 */
class Object {
public:
  virtual ~Object() {}

  /**
   * Convert a generic object into an array of objects. This is useful for dealing
   * with arrays of arrays.
   * @return std::vector<ObjectSharedPtr> the converted object.
   */
  virtual std::vector<ObjectSharedPtr> asObjectArray() const PURE;

  /**
   * Get a boolean value by name.
   * @param name supplies the key name.
   * @return bool the value.
   */
  virtual bool getBoolean(const std::string& name) const PURE;

  /**
   * Get a boolean value by name.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if the name does not exist.
   * @return bool the value.
   */
  virtual bool getBoolean(const std::string& name, bool default_value) const PURE;

  /**
   * Get an integer value by name.
   * @param name supplies the key name.
   * @return int64_t the value.
   */
  virtual int64_t getInteger(const std::string& name) const PURE;

  /**
   * Get an integer value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return int64_t the value.
   */
  virtual int64_t getInteger(const std::string& name, int64_t default_value) const PURE;

  /**
   * Get a sub-object by name.
   * @param name supplies the key name.
   * @param allow_empty supplies whether to return an empty object if the key does not
   * exist.
   * @return ObjectObjectSharedPtr the sub-object.
   */
  virtual ObjectSharedPtr getObject(const std::string& name, bool allow_empty = false) const PURE;

  /**
   * Get an array by name.
   * @param name supplies the key name.
   * @return std::vector<ObjectSharedPtr> the array of JSON  objects.
   */
  virtual std::vector<ObjectSharedPtr> getObjectArray(const std::string& name) const PURE;

  /**
   * Get a string value by name.
   * @param name supplies the key name.
   * @return std::string the value.
   */
  virtual std::string getString(const std::string& name) const PURE;

  /**
   * Get a string value by name or return a default if name does not exist.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return std::string the value.
   */
  virtual std::string getString(const std::string& name,
                                const std::string& default_value) const PURE;

  /**
   * Get a string array by name.
   * @param name supplies the key name.
   * @return std::vector<std::string> the array of strings.
   */
  virtual std::vector<std::string> getStringArray(const std::string& name) const PURE;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @return double the value.
   */
  virtual double getDouble(const std::string& name) const PURE;

  /**
   * Get a double value by name.
   * @param name supplies the key name.
   * @param default_value supplies the value to return if name does not exist.
   * @return double the value.
   */
  virtual double getDouble(const std::string& name, double default_value) const PURE;

  /**
   * @return a hash of the JSON object. This is a hash of each nested element in stable order.
   *         It does not consider white space that was originally in the parsed JSON.
   */
  virtual uint64_t hash() const PURE;

  /**
   * Iterate over key-value pairs in an Object and call callback on each pair.
   */
  virtual void iterate(const ObjectCallback& callback) const PURE;

  /**
   * @return TRUE if the Object contains the key.
   * @param name supplies the key name to lookup.
   */
  virtual bool hasObject(const std::string& name) const PURE;

  /**
   * Validates JSON object against passed in schema.
   * @param schema supplies the schema in string format. A Json::Exception will be thrown if
   *        the JSON object doesn't conform to the supplied schema or the schema itself is not
   *        valid.
   */
  virtual void validateSchema(const std::string& schema) const PURE;

  /**
   * @return the value of the object as a string
   */
  virtual std::string asString() const PURE;

  /**
   * @return true if the JSON object is empty;
   */
  virtual bool empty() const PURE;
};

} // Json
} // Envoy
