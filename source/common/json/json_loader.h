#pragma once

#include <list>
#include <string>

#include "envoy/api/api.h"
#include "envoy/json/json_object.h"

namespace Envoy {
namespace Json {

class Factory {
public:
  /**
   * Constructs a Json Object from a file.
   */
  static ObjectSharedPtr loadFromFile(const std::string& file_path, Api::Api& api);

  /**
   * Constructs a Json Object from a string.
   */
  static ObjectSharedPtr loadFromString(const std::string& json);

  /**
   * Constructs a Json Object from a YAML string.
   */
  static ObjectSharedPtr loadFromYamlString(const std::string& yaml);
};

} // namespace Json
} // namespace Envoy
