#pragma once

#include <string>

#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/formatter/substitution_formatter.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Formatter {

/**
 * Utilities for using envoy::config::core::v3::SubstitutionFormatString
 */
class SubstitutionFormatStringUtils {
public:
  /**
   * Generate a formatter object from config SubstitutionFormatString.
   */
  static FormatterPtr
  fromProtoConfig(const envoy::config::core::v3::SubstitutionFormatString& config);

  /**
   * Generate a Json formatter object from proto::Struct config
   */
  static FormatterPtr createJsonFormatter(const ProtobufWkt::Struct& struct_format,
                                          bool preserve_types, bool omit_empty_values);

  /**
   * Returns content-type from config SubstitutionFormatString
   */
  static std::string
  getContentType(const envoy::config::core::v3::SubstitutionFormatString& config);
};

} // namespace Formatter
} // namespace Envoy
