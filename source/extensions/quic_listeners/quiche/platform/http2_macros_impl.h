#pragma once

#include "common/common/assert.h"

#include "absl/base/macros.h"

// NOLINT(namespace-envoy)

#define HTTP2_FALLTHROUGH_IMPL ABSL_FALLTHROUGH_INTENDED
#define HTTP2_UNREACHABLE_IMPL() NOT_REACHED_GCOVR_EXCL_LINE
#define HTTP2_DIE_IF_NULL_IMPL(ptr) ABSL_DIE_IF_NULL(ptr)
