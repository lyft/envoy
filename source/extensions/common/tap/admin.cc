#include "extensions/common/tap/admin.h"

#include "envoy/admin/v2alpha/tap.pb.h"
#include "envoy/admin/v2alpha/tap.pb.validate.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(tap_admin_handler);

AdminHandlerSharedPtr AdminHandler::getSingleton(Server::Admin& admin,
                                                 Singleton::Manager& singleton_manager,
                                                 Event::Dispatcher& main_thread_dispatcher) {
  return singleton_manager.getTyped<AdminHandler>(
      SINGLETON_MANAGER_REGISTERED_NAME(tap_admin_handler), [&admin, &main_thread_dispatcher] {
        return std::make_shared<AdminHandler>(admin, main_thread_dispatcher);
      });
}

AdminHandler::AdminHandler(Server::Admin& admin, Event::Dispatcher& main_thread_dispatcher)
    : admin_(admin), main_thread_dispatcher_(main_thread_dispatcher) {
  const bool rc =
      admin_.addHandler("/tap", "tap filter control", MAKE_ADMIN_HANDLER(handler), true, true);
  RELEASE_ASSERT(rc, "/tap admin endpoint is taken");
}

AdminHandler::~AdminHandler() {
  const bool rc = admin_.removeHandler("/tap");
  ASSERT(rc);
}

Http::Code AdminHandler::handler(absl::string_view, Http::HeaderMap&, Buffer::Instance& response,
                                 Server::AdminStream& admin_stream) {
  if (attached_request_.has_value()) {
    // TODO(mattlklein123): Consider supporting concurrent admin /tap streams. Right now we support
    // a single stream as a simplification.
    return badRequest(response, "An attached /tap admin stream already exists. Detach it.");
  }

  if (admin_stream.getRequestBody() == nullptr) {
    return badRequest(response, "/tap requires a JSON/YAML body");
  }

  envoy::admin::v2alpha::TapRequest tap_request;
  try {
    MessageUtil::loadFromYamlAndValidate(admin_stream.getRequestBody()->toString(), tap_request);
  } catch (EnvoyException& e) {
    return badRequest(response, e.what());
  }

  ENVOY_LOG(debug, "tap admin request for config_id={}", tap_request.config_id());
  if (config_id_map_.count(tap_request.config_id()) == 0) {
    return badRequest(
        response, fmt::format("Unknown config id '{}'. No extension has registered with this id.",
                              tap_request.config_id()));
  }
  for (auto config : config_id_map_[tap_request.config_id()]) {
    config->newTapConfig(std::move(*tap_request.mutable_tap_config()), this);
  }

  admin_stream.setEndStreamOnComplete(false);
  admin_stream.addOnDestroyCallback([this] {
    for (auto config : config_id_map_[attached_request_.value().config_id_]) {
      ENVOY_LOG(debug, "detach tap admin request for config_id={}",
                attached_request_.value().config_id_);
      config->clearTapConfig();
      attached_request_ = absl::nullopt;
    }
  });
  attached_request_.emplace(tap_request.config_id(), &admin_stream);
  return Http::Code::OK;
}

Http::Code AdminHandler::badRequest(Buffer::Instance& response, absl::string_view error) {
  ENVOY_LOG(debug, "handler bad request: {}", error);
  response.add(error);
  return Http::Code::BadRequest;
}

void AdminHandler::registerConfig(ExtensionConfig& config, const std::string& config_id) {
  ASSERT(!config_id.empty());
  ASSERT(config_id_map_[config_id].count(&config) == 0);
  config_id_map_[config_id].insert(&config);
}

void AdminHandler::unregisterConfig(ExtensionConfig& config) {
  ASSERT(!config.adminId().empty());
  std::string admin_id(config.adminId());
  ASSERT(config_id_map_[admin_id].count(&config) == 1);
  config_id_map_[admin_id].erase(&config);
  if (config_id_map_[admin_id].empty()) {
    config_id_map_.erase(admin_id);
  }
}

void AdminHandler::submitBufferedTrace(
    std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper> trace, uint64_t) {
  ENVOY_LOG(debug, "admin submitting buffered trace to main thread");
  main_thread_dispatcher_.post([this, trace]() {
    if (attached_request_.has_value()) {
      ENVOY_LOG(debug, "admin writing buffered trace to response");
      Buffer::OwnedImpl json_trace{MessageUtil::getJsonStringFromMessage(*trace, true, true)};
      attached_request_.value().admin_stream_->getDecoderFilterCallbacks().encodeData(json_trace,
                                                                                      false);
    }
  });
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
