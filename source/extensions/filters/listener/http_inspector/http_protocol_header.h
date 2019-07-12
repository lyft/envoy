#pragma once

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * Http2 magic string.
 */
constexpr absl::string_view HTTP2_CONNECTION_PREFACE =
    "\x50\x52\x49\x20\x2a\x20\x48\x54\x54\x50\x2f\x32\x2e"
    "\x30\x0d\x0a\x0d\x0a\x53\x4d\x0d\x0a\x0d\x0a";

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
