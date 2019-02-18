#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/cluster_factory.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/network/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/upstream_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "extensions/clusters/well_known_names.h"

#include "server/init_manager_impl.h"

namespace Envoy {
namespace Upstream {

class ClusterFactoryContextImpl : public ClusterFactoryContext {

public:
  ClusterFactoryContextImpl(ClusterManager& cluster_manager, Stats::Store& stats,
  ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
  Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
  Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
  AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
    Server::Admin& admin, Singleton::Manager& singleton_manager,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api, Api::Api& api)
                            : cluster_manager_(cluster_manager), stats_(stats), tls_(tls), dns_resolver_(dns_resolver),
                            ssl_context_manager_(ssl_context_manager), runtime_(runtime), random_(random), 
                            dispatcher_(dispatcher), log_manager_(log_manager), local_info_(local_info), admin_(admin),
                            singleton_manager_(singleton_manager), outlier_event_logger_(outlier_event_logger), 
                            added_via_api_(added_via_api), api_(api) {}

  ClusterManager& clusterManager() override { return cluster_manager_;}
  Stats::Store& stats() override { return stats_;}
  ThreadLocal::Instance& tls() override { return tls_;}
  Network::DnsResolverSharedPtr dnsResolver() override { return dns_resolver_;}
  Ssl::ContextManager& sslContextManager() override { return ssl_context_manager_;}
  Runtime::Loader& runtime() override { return runtime_;}
  Runtime::RandomGenerator& random() override { return random_;}
  Event::Dispatcher& dispatcher() override { return dispatcher_;}
  AccessLog::AccessLogManager& logManager() override { return log_manager_;}
  const LocalInfo::LocalInfo& localInfo() override { return local_info_;}
  Server::Admin& admin() override { return admin_;}
  Singleton::Manager& singletonManager() override { return singleton_manager_;}
  Outlier::EventLoggerSharedPtr outlierEventLogger() override { return outlier_event_logger_;}
  bool addedViaApi() override { return added_via_api_;}
  Api::Api& api() override { return api_;}

private:
  ClusterManager & cluster_manager_;
  Stats::Store &stats_;
  ThreadLocal::Instance &tls_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager &ssl_context_manager_;
  Runtime::Loader &runtime_;
  Runtime::RandomGenerator &random_;
  Event::Dispatcher &dispatcher_;
  AccessLog::AccessLogManager &log_manager_;
  const LocalInfo::LocalInfo &local_info_;
  Server::Admin &admin_;
  Singleton::Manager &singleton_manager_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  bool added_via_api_;
  Api::Api &api_;
};

class ClusterFactoryImplBase : public ClusterFactory {
public:

  static ClusterSharedPtr create(const envoy::api::v2::Cluster& cluster, ClusterManager& cluster_manager, Stats::Store& stats,
         ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
         Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
         Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
         AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
         Server::Admin& admin, Singleton::Manager& singleton_manager,
         Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api, Api::Api& api);

  ClusterSharedPtr create(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context) override;

  Network::DnsResolverSharedPtr selectDnsResolver(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context);

  virtual ClusterImplBaseSharedPtr createClusterImpl(const envoy::api::v2::Cluster& cluster,
    ClusterFactoryContext& context, Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) PURE;

  std::string name() override { return name_; }

protected:
  ClusterFactoryImplBase(const std::string name) : name_(name) {}

private:
  const std::string name_;
};

class StaticClusterFactory : public ClusterFactoryImplBase {
public:
  StaticClusterFactory() : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().Static) { }

  ClusterImplBaseSharedPtr createClusterImpl(const envoy::api::v2::Cluster& cluster,
    ClusterFactoryContext& context, Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) override;
};

class StrictDnsClusterFactory : public ClusterFactoryImplBase {
public:
  StrictDnsClusterFactory() : ClusterFactoryImplBase(Extensions::Clusters::ClusterTypes::get().StrictDns) { }

  ClusterImplBaseSharedPtr createClusterImpl(const envoy::api::v2::Cluster& cluster,
                                             ClusterFactoryContext& context, Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
                                             Stats::ScopePtr&& stats_scope) override;
};

} // namespace Upstream
} // namespace Envoy
