#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/strings/string_view.h"

namespace quic {

void QuicSaveTestOutputImpl(absl::string_view filename, absl::string_view data);

bool QuicLoadTestOutputImpl(absl::string_view filename, std::string* data);

void QuicRecordTraceImpl(absl::string_view identifier, absl::string_view data);

} // namespace quic
