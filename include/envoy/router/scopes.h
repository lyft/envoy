#pragma once

#include <memory>

#include "envoy/config/config_provider.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * The scoped routing configuration.
 */
class ScopedConfig : public Envoy::Config::ConfigProvider::Config {
public:
  virtual ~ScopedConfig() {}

  /**
   * Based on the incoming HTTP request headers, returns the configuration to use for selecting a
   * target route (to either a route entry or a direct response entry).
   * @param headers the request headers to match the scoped routing configuration against.
   * @return ConfigConstSharedPtr the router's Config matching the request headers.
   */
  virtual ConfigConstSharedPtr getRouterConfig(const Http::HeaderMap& headers) const PURE;
};

using ScopedConfigConstSharedPtr = std::shared_ptr<const ScopedConfig>;

} // namespace Router
} // namespace Envoy
