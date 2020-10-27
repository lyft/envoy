#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "common/common/empty_string.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// The maximum size for this variable.
constexpr size_t TopRequirementNameForDebugSize = 100;

} // namespace

void FilterConfigImpl::init() {
  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  // Note: `this` and `context` have a a lifetime of the listener.
  // That may be shorter of the tls callback if the listener is torn shortly after it is created.
  // We use a shared pointer to make sure this object outlives the tls callbacks.
  auto shared_this = shared_from_this();
  tls_->set([shared_this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalCache>(shared_this->proto_config_, shared_this->time_source_,
                                              shared_this->api_);
  });

  for (const auto& rule : proto_config_.rules()) {
    rule_pairs_.emplace_back(Matcher::create(rule),
                             Verifier::create(rule.requires(), proto_config_.providers(), *this));
  }

  if (proto_config_.has_filter_state_rules()) {
    filter_state_name_ = proto_config_.filter_state_rules().name();
    for (const auto& it : proto_config_.filter_state_rules().requires()) {
      filter_state_verifiers_.emplace(
          it.first, Verifier::create(it.second, proto_config_.providers(), *this));
    }
  }

  for (const auto& it : proto_config_.requirement_map()) {
    if (top_requirement_names_for_debug_.size() < TopRequirementNameForDebugSize) {
      if (top_requirement_names_for_debug_.empty()) {
        top_requirement_names_for_debug_ = it.first;
      } else {
        absl::StrAppend(&top_requirement_names_for_debug_, ",", it.first);
      }
    }
    name_verifiers_.emplace(it.first,
                            Verifier::create(it.second, proto_config_.providers(), *this));
  }
}

std::pair<const Verifier*, std::string>
FilterConfigImpl::findPerRouteVerifier(const PerRouteFilterConfig& per_route) const {
  if (per_route.config().bypass()) {
    return std::make_pair(nullptr, EMPTY_STRING);
  }

  const auto& it = name_verifiers_.find(per_route.config().requirement_name());
  if (it != name_verifiers_.end()) {
    return std::make_pair(it->second.get(), EMPTY_STRING);
  }

  return std::make_pair(
      nullptr, absl::StrCat("Wrong requirement_name: ", per_route.config().requirement_name(),
                            ". Correct names are: ", top_requirement_names_for_debug_));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
