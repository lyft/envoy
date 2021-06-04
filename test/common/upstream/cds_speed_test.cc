// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/grpc_mux_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/upstream/static_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "test/benchmark/main.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

using ::benchmark::State;
using Envoy::benchmark::skipExpensiveBenchmarks;

namespace Envoy {
namespace Upstream {

class CdsSpeedTest {
public:
  CdsSpeedTest(State& state)
      : state_(state), type_url_("type.googleapis.com/envoy.config.cluster.v3.Cluster"),
        subscription_stats_(Config::Utility::generateStats(stats_)),
        api_(Api::createApiForTest(stats_)), async_client_(new Grpc::MockAsyncClient()),
        grpc_mux_(new Config::GrpcMuxImpl(
            local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                "envoy.service.cluster.v3.ClusterDiscoveryService.StreamClusters"),
            envoy::config::core::v3::ApiVersion::AUTO, random_, stats_, {}, true)) {

    resetCluster();

    cluster_->initialize([this] { initialized_ = true; });
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(testing::Return(&async_stream_));
    subscription_->start({"fare"});
  }

  void resetCluster() {
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    static_cluster_ = buildStaticCluster("staticcluster", 1024, "127.0.0.1");
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", static_cluster_.alt_stat_name().empty() ? static_cluster_.name()
                                                               : static_cluster_.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    cluster_ = std::make_shared<StaticClusterImpl>(static_cluster_, runtime_, factory_context,
                                                   std::move(scope), false);
    EXPECT_EQ(Envoy::Upstream::Cluster::InitializePhase::Primary, cluster_->initializePhase());
    callbacks_ = cm_.subscription_factory_.callbacks_;
    subscription_ = std::make_unique<Config::GrpcSubscriptionImpl>(
        grpc_mux_, *callbacks_, resource_decoder_, subscription_stats_, type_url_, dispatcher_,
        std::chrono::milliseconds(), false);
  }

  void clusterHelper(bool ignore_unknown_dynamic_fields, size_t num_clusters) {
    auto response = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
    response->set_type_url(type_url_);
    response->set_version_info(fmt::format("version-{}", version_++));

    // make a pile of static clusters and add them to the response
    for (size_t i = 0; i < num_clusters; ++i) {
      envoy::config::cluster::v3::Cluster cluster = buildStaticCluster(
          "cluster_" + std::to_string(i), i % 60000, "10.0.1." + std::to_string(i / 60000));

      auto* resource = response->mutable_resources()->Add();
      resource->PackFrom(cluster);
      RELEASE_ASSERT(resource->type_url() == "type.googleapis.com/envoy.config.cluster.v3.Cluster",
                     "");
    }

    validation_visitor_.setSkipValidation(ignore_unknown_dynamic_fields);

    state_.SetComplexityN(num_clusters);
    state_.ResumeTiming();
    grpc_mux_->grpcStreamForTest().onReceiveMessage(std::move(response));
    state_.PauseTiming();
  }

  // this is ConfigHelper::buildStaticCluster, but without YAML, in the interest of efficiency.
  envoy::config::cluster::v3::Cluster buildStaticCluster(const std::string& name, const int port,
                                                         const std::string& address) {
    envoy::config::cluster::v3::Cluster cluster;
    cluster.set_name(name);
    cluster.mutable_connect_timeout()->set_seconds(5);
    cluster.set_type(envoy::config::cluster::v3::Cluster::STATIC);
    cluster.set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
    auto* load_assignment = cluster.mutable_load_assignment();
    load_assignment->set_cluster_name(name);
    auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
    auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
    socket_address->set_address(address);
    socket_address->set_port_value(port);

    return cluster;
  }

  State& state_;
  const std::string type_url_;
  uint64_t version_{};
  bool initialized_{};
  Stats::TestSymbolTable symbol_table_{};
  Stats::AllocatorImpl stats_allocator_{*symbol_table_};
  Stats::ThreadLocalStoreImpl stats_{stats_allocator_};
  Config::SubscriptionStats subscription_stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::config::cluster::v3::Cluster static_cluster_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  ClusterImplBaseSharedPtr cluster_;
  Config::SubscriptionCallbacks* callbacks_{};
  Config::OpaqueResourceDecoderImpl<envoy::config::cluster::v3::Cluster> resource_decoder_{
      validation_visitor_, "name"};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  ProtobufMessage::MockValidationVisitor validation_visitor_;
  Api::ApiPtr api_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  Config::GrpcMuxImplSharedPtr grpc_mux_;
  Config::GrpcSubscriptionImplPtr subscription_;
};

} // namespace Upstream
} // namespace Envoy

static void addClusters(State& state) {
  Envoy::Upstream::CdsSpeedTest speed_test(state);
  // if we've been instructed to skip tests, only run once no matter the argument:
  const uint32_t num_clusters = skipExpensiveBenchmarks() ? 1 : state.range(1);
  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    // timing is resumed within the helper:
    state.PauseTiming();
    speed_test.clusterHelper(state.range(0), num_clusters);
    state.ResumeTiming();
  }
}

BENCHMARK(addClusters)
    ->Ranges({{false, true}, {64, 100000}})
    ->Unit(benchmark::kMillisecond)
    ->Complexity();

// Look for suboptimal behavior when receiving two identical updates
static void duplicateUpdate(State& state) {
  Envoy::Upstream::CdsSpeedTest speed_test(state);
  const uint32_t num_clusters = skipExpensiveBenchmarks() ? 1 : state.range(0);
  for (auto _ : state) { // NOLINT(clang-analyzer-deadcode.DeadStores)
    state.PauseTiming();

    speed_test.clusterHelper(true, num_clusters);
    speed_test.clusterHelper(true, num_clusters);
    state.ResumeTiming();
  }
}

BENCHMARK(duplicateUpdate)->Range(64, 100000)->Unit(benchmark::kMillisecond)->Complexity();
