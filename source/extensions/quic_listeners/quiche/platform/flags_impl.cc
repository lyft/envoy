// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include <set>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

namespace quiche {

namespace {

absl::flat_hash_map<std::string, Flag*> MakeFlagMap() {
  absl::flat_hash_map<std::string, Flag*> flags;

#define QUIC_FLAG(flag, ...) flags.emplace(flag->name(), flag);
#include "quiche/quic/core/quic_flags_list.h"
  QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_true, true)
  QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_false, false)
  QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_true, true)
#undef QUIC_FLAG

#define QUIC_PROTOCOL_FLAG(type, flag, ...) flags.emplace(FLAGS_##flag->name(), FLAGS_##flag);
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

  return flags;
}

} // namespace

// static
FlagRegistry& FlagRegistry::GetInstance() {
  static auto* instance = new FlagRegistry();
  return *instance;
}

FlagRegistry::FlagRegistry() : flags_(MakeFlagMap()) {}

void FlagRegistry::ResetFlags() const {
  for (auto& kv : flags_) {
    kv.second->ResetValue();
  }
}

Flag* FlagRegistry::FindFlag(const std::string& name) const {
  auto it = flags_.find(name);
  return (it != flags_.end()) ? it->second : nullptr;
}

template <> bool TypedFlag<bool>::SetValueFromString(const std::string& value_str) {
  static const auto* kTrueValues = new std::set<std::string>({"1", "t", "true", "y", "yes"});
  static const auto* kFalseValues = new std::set<std::string>({"0", "f", "false", "n", "no"});
  auto lower = absl::AsciiStrToLower(value_str);
  if (kTrueValues->find(lower) != kTrueValues->end()) {
    SetValue(true);
    return true;
  }
  if (kFalseValues->find(lower) != kFalseValues->end()) {
    SetValue(false);
    return true;
  }
  return false;
}

template <> bool TypedFlag<int32_t>::SetValueFromString(const std::string& value_str) {
  int32_t value;
  if (absl::SimpleAtoi(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<int64_t>::SetValueFromString(const std::string& value_str) {
  int64_t value;
  if (absl::SimpleAtoi(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<double>::SetValueFromString(const std::string& value_str) {
  double value;
  if (absl::SimpleAtod(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

template <> bool TypedFlag<std::string>::SetValueFromString(const std::string& value_str) {
  SetValue(value_str);
  return true;
}

template <> bool TypedFlag<unsigned long>::SetValueFromString(const std::string& value_str) {
  unsigned long value;
  if (absl::SimpleAtoi(value_str, &value)) {
    SetValue(value);
    return true;
  }
  return false;
}

// Flag definitions
#define QUIC_FLAG(flag, value) TypedFlag<bool>* flag = new TypedFlag<bool>(#flag, value, "");
#include "quiche/quic/core/quic_flags_list.h"
QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_reloadable_flag_spdy_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_restart_flag_spdy_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_reloadable_flag_http2_testonly_default_true, true)
QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_false, false)
QUIC_FLAG(FLAGS_quic_restart_flag_http2_testonly_default_true, true)

#undef QUIC_FLAG

#define STRINGIFY(X) #X

#define DEFINE_QUIC_PROTOCOL_FLAG_IMPL(type, flag, value, help)                                    \
  TypedFlag<type>* FLAGS_##flag = new TypedFlag<type>(STRINGIFY(FLAGS_##flag), value, help);

#define DEFINE_QUIC_PROTOCOL_FLAG_SINGLE_VALUE(type, flag, value, doc)                             \
  DEFINE_QUIC_PROTOCOL_FLAG_IMPL(type, flag, value, doc)

#define DEFINE_QUIC_PROTOCOL_FLAG_TWO_VALUES(type, flag, internal_value, external_value, doc)      \
  DEFINE_QUIC_PROTOCOL_FLAG_IMPL(type, flag, internal_value, doc)

// Select the right macro based on the number of arguments.
#define GET_6TH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, ...) arg6
#define QUIC_PROTOCOL_FLAG_MACRO_CHOOSER(...)                                                      \
  GET_6TH_ARG(__VA_ARGS__, DEFINE_QUIC_PROTOCOL_FLAG_TWO_VALUES,                                   \
              DEFINE_QUIC_PROTOCOL_FLAG_SINGLE_VALUE)
#define QUIC_PROTOCOL_FLAG(...) QUIC_PROTOCOL_FLAG_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

} // namespace quiche
