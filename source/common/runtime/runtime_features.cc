#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Runtime {

// Add additional features here to enable the new code paths by default.
//
// Per documentation in CONTRIBUTING.md is is expected that new high risk code paths be guarded
// by runtime feature guards, i.e
//
// if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.my_feature_name")) {
//   [new code path]
// else {
//   [old_code_path]
// }
//
// Runtime features are false by default, so the old code path is exercised.
// To make a runtime feature true by default, add it to the array below.
// New features should be true-by-default for an Envoy release cycle before the
// old code path is removed.
//
// If issues are found that that require a runtime feature to be disabled, it should be reported
// ASAP by filing a bug on github. Overriding non-buggy code is strongly discouraged to avoid the
// problem of the bugs being found after the old code path has been removed.
constexpr const char* runtime_features[] = {
    // Enabled
    "envoy.reloadable_features.test_feature_true",
};

// This is a list of configuration fields which are disallowed by default in Envoy
//
// By default, use of proto fields marked as deprecated in their api/.../*.proto file will result
// in a logged warning, so that Envoy users have a warning that they are using deprecated fields.
//
// During the Envoy release cycle, the maintainer team runs a script which will upgrade currently
// deprecated features to be disallowed (adding them to the list below) at which point use of said
// feature will cause a hard-failure (ProtoValidationException) instead of a logged warning.
//
// The release cycle after a feature has been marked disallowed, it is officially removable, and
// the maintainer team will run a script creating a tracking issue for proto and code clean up.
//
// TODO(alyssawilk) handle deprecation of reloadable_features and update the above comment. Ideally
// runtime override of a deprecated feature will log(warn) on runtime-load if not deprecated
// and hard-fail once it has been deprecated.

constexpr const char* disallowed_features[] = {
    // Acts as both a test entry for deprecated.proto and a marker for the Envoy
    // deprecation scripts.
    "envoy.deprecated_features.deprecated.proto:is_deprecated_fatal",
    "envoy.deprecated_features.config_source.proto:UNSUPPORTED_REST_LEGACY",
    "envoy.deprecated_features.ext_authz.proto:use_alpha",
    "envoy.deprecated_features.route.proto:enabled",
    "envoy.deprecated_features.tcp_proxy.proto:deprecated_v1",
    "envoy.deprecated_features.fault.proto:type",
    "envoy.deprecated_features.cds.proto:hosts",
    "envoy.deprecated_features.route.proto:runtime_key",
};

RuntimeFeatures::RuntimeFeatures() {
  for (auto& feature : disallowed_features) {
    disallowed_features_.insert(feature);
  }
  for (auto& feature : runtime_features) {
    enabled_features_.insert(feature);
  }
}

} // namespace Runtime
} // namespace Envoy
