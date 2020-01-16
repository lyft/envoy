/*
 * Common enumerations available to WASM modules and shared with sandbox.
 */
// NOLINT(namespace-envoy)

#pragma once

#include <string>

enum class WasmResult : uint32_t {
  Ok = 0,
  // The result could not be found, e.g. a provided key did not appear in a table.
  NotFound = 1,
  // An argument was bad, e.g. did not not conform to the required range.
  BadArgument = 2,
  // A protobuf could not be serialized.
  SerializationFailure = 3,
  // A protobuf could not be parsed.
  ParseFailure = 4,
  // A provided expression (e.g. "foo.bar") was illegal or unrecognized.
  BadExpression = 5,
  // A provided memory range was not legal.
  InvalidMemoryAccess = 6,
  // Data was requested from an empty container.
  Empty = 7,
  // The provided CAS did not match that of the stored data.
  CasMismatch = 8,
  // Returned result was unexpected, e.g. of the incorrect size.
  ResultMismatch = 9,
  // Internal failure: trying check logs of the surrounding system.
  InternalFailure = 10,
  // The connection/stream/pipe was broken/closed unexpectedly.
  BrokenConnection = 11,
};

#define _CASE(_e)                                                                                  \
  case WasmResult::_e:                                                                             \
    return #_e
inline std::string toString(WasmResult r) {
  switch (r) {
    _CASE(Ok);
    _CASE(NotFound);
    _CASE(BadArgument);
    _CASE(SerializationFailure);
    _CASE(ParseFailure);
    _CASE(BadExpression);
    _CASE(InvalidMemoryAccess);
    _CASE(Empty);
    _CASE(CasMismatch);
    _CASE(ResultMismatch);
    _CASE(InternalFailure);
    _CASE(BrokenConnection);
  }
}
#undef _CASE

enum class OnVmStartResult : uint32_t {
  BadConfiguration = 0,
  Ok = 1,
};

enum class OnValidateConfigurationResult : uint32_t {
  BadConfiguration = 0,
  Ok = 1,
};

enum class OnConfigureResult : uint32_t {
  BadConfiguration = 0,
  Ok = 1,
};

enum class OnDoneResult : uint32_t {
  NotDone = 0,
  Done = 1,
};
