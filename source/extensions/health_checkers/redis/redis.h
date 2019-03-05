#pragma once

#include "envoy/config/health_checker/redis/v2/redis.pb.validate.h"

#include "common/upstream/health_checker_base_impl.h"

#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

/**
 * Redis health checker implementation. Sends PING and expects PONG.
 */
class RedisHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  RedisHealthChecker(
      const Upstream::Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
      const envoy::config::health_checker::redis::v2::Redis& redis_config,
      Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
      Upstream::HealthCheckEventLoggerPtr&& event_logger,
      Extensions::NetworkFilters::Common::Redis::ClientFactory& client_factory);

  static const NetworkFilters::Common::Redis::RespValue& pingHealthCheckRequest() {
    static HealthCheckRequest* request = new HealthCheckRequest();
    return request->request_;
  }

  static const NetworkFilters::Common::Redis::RespValue&
  existsHealthCheckRequest(const std::string& key) {
    static HealthCheckRequest* request = new HealthCheckRequest(key);
    return request->request_;
  }

protected:
  envoy::data::core::v2alpha::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v2alpha::HealthCheckerType::REDIS;
  }

private:
  struct RedisActiveHealthCheckSession
      : public ActiveHealthCheckSession,
        public Extensions::NetworkFilters::Common::Redis::Config,
        public Extensions::NetworkFilters::Common::Redis::PoolCallbacks,
        public Network::ConnectionCallbacks {
    RedisActiveHealthCheckSession(RedisHealthChecker& parent, const Upstream::HostSharedPtr& host);
    ~RedisActiveHealthCheckSession();
    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;

    // Extensions::NetworkFilters::Common::Redis::Config
    bool disableOutlierEvents() const override { return true; }
    std::chrono::milliseconds opTimeout() const override {
      // Allow the main Health Check infra to control timeout.
      return parent_.timeout_ * 2;
    }
    bool enableHashtagging() const override { return false; }

    // Extensions::NetworkFilters::Common::Redis::PoolCallbacks
    void onResponse(NetworkFilters::Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    RedisHealthChecker& parent_;
    Extensions::NetworkFilters::Common::Redis::ClientPtr client_;
    Extensions::NetworkFilters::Common::Redis::PoolRequest* current_request_{};
  };

  enum class Type { Ping, Exists };

  struct HealthCheckRequest {
    HealthCheckRequest(const std::string& key);
    HealthCheckRequest();

    NetworkFilters::Common::Redis::RespValue request_;
  };

  typedef std::unique_ptr<RedisActiveHealthCheckSession> RedisActiveHealthCheckSessionPtr;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<RedisActiveHealthCheckSession>(*this, host);
  }

  Extensions::NetworkFilters::Common::Redis::ClientFactory& client_factory_;
  Type type_;
  const std::string key_;
};

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
