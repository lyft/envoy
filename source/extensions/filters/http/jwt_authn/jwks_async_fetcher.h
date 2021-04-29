#pragma once

#include <chrono>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/server/factory_context.h"

#include "common/common/logger.h"
#include "common/init/target_impl.h"

#include "extensions/filters/http/common/jwks_fetcher.h"
#include "extensions/filters/http/jwt_authn/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 *  CreateJwksFetcherCb is a callback interface for creating a JwksFetcher instance.
 */
using CreateJwksFetcherCb = std::function<Common::JwksFetcherPtr(Upstream::ClusterManager&)>;
/**
 *  JwksDoneFetched is a callback interface to set a Jwks when fetch is done.
 */
using JwksDoneFetched = std::function<void(google::jwt_verify::JwksPtr&& jwks)>;

// This class handles fetching Jwks asynchronously.
// At its constructor, it will start to fetch Jwks, register with init_manager
// and handle fetching response. When cache is expired, it will fetch again.
// When a Jwks is fetched, done_fn is called to set the Jwks.
class JwksAsyncFetcher : public Logger::Loggable<Logger::Id::jwt>,
                         public Common::JwksFetcher::JwksReceiver {
public:
  JwksAsyncFetcher(const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks,
                   Server::Configuration::FactoryContext& context, CreateJwksFetcherCb fetcher_fn,
                   JwtAuthnFilterStats& stats, JwksDoneFetched done_fn);

  ~JwksAsyncFetcher();

  // Get the remote Jwks cache duration.
  static std::chrono::seconds
  getCacheDuration(const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks);

private:
  // Start to fetch Jwks
  void refresh();
  // Handle fetch done.
  void handle_fetch_done();

  // Override the functions from Common::JwksFetcher::JwksReceiver
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override;
  void onJwksError(Failure reason) override;

  // the remote Jwks config
  const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks_;
  // the factory context
  Server::Configuration::FactoryContext& context_;
  // the jwks fetcher creator function
  CreateJwksFetcherCb fetcher_fn_;
  // stats
  JwtAuthnFilterStats& stats_;
  // the Jwks done function.
  JwksDoneFetched done_fn_;

  // The Jwks fetcher object
  Common::JwksFetcherPtr fetcher_;

  Envoy::Event::TimerPtr refresh_timer_;
  std::unique_ptr<Init::TargetImpl> init_target_;

  uint32_t fail_retry_count_{};
  std::chrono::seconds refresh_duration_;
  // Used in logs.
  std::string debug_name_;
};

using JwksAsyncFetcherPtr = std::unique_ptr<JwksAsyncFetcher>;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
