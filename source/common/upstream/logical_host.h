#pragma once

#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * A host implementation that can have its address changed in order to create temporal "real"
 * hosts.
 */
class LogicalHost : public HostImpl {
public:
  LogicalHost(const ClusterInfoConstSharedPtr& cluster, const std::string& hostname,
              const Network::Address::InstanceConstSharedPtr& address,
              const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint,
              const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint)
      : HostImpl(cluster, hostname, address, lb_endpoint.metadata(),
                 lb_endpoint.load_balancing_weight().value(), locality_lb_endpoint.locality(),
                 lb_endpoint.endpoint().health_check_config(), locality_lb_endpoint.priority(),
                 lb_endpoint.health_status()) {}

  // Set the new address. Updates are typically rare so a R/W lock is used to address updates.
  // Note that the health check address update requires no lock to be held since it is only
  // used on the main thread.
  void setNewAddress(const Network::Address::InstanceConstSharedPtr& address,
                     const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint) {
    const auto& port_value = lb_endpoint.endpoint().health_check_config().port_value();
    health_check_address_ =
        port_value == 0 ? address : Network::Utility::getAddressWithPort(*address, port_value);

    absl::WriterMutexLock lock(&address_lock_);
    address_ = address;
  }

  // Upstream::Host
  CreateConnectionData createConnection(
      Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
      Network::TransportSocketOptionsSharedPtr transport_socket_options) const override;

  // Upstream::HostDescription
  Network::Address::InstanceConstSharedPtr address() const override {
    absl::ReaderMutexLock lock(&address_lock_);
    return HostImpl::address();
  }

private:
  mutable absl::Mutex address_lock_;
};

using LogicalHostSharedPtr = std::shared_ptr<LogicalHost>;

/**
 * A real host that forwards most of its calls to a logical host, but returns a snapped address.
 */
class RealHostDescription : public HostDescription {
public:
  RealHostDescription(Network::Address::InstanceConstSharedPtr address,
                      HostConstSharedPtr logical_host)
      : address_(address), logical_host_(logical_host) {}

  // Upstream:HostDescription
  bool canary() const override { return false; }
  void canary(bool) override {}
  const std::shared_ptr<envoy::api::v2::core::Metadata> metadata() const override {
    return logical_host_->metadata();
  }
  void metadata(const envoy::api::v2::core::Metadata&) override {}

  const ClusterInfo& cluster() const override { return logical_host_->cluster(); }
  HealthCheckHostMonitor& healthChecker() const override { return logical_host_->healthChecker(); }
  Outlier::DetectorHostMonitor& outlierDetector() const override {
    return logical_host_->outlierDetector();
  }
  const HostStats& stats() const override { return logical_host_->stats(); }
  const std::string& hostname() const override { return logical_host_->hostname(); }
  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const envoy::api::v2::core::Locality& locality() const override {
    return logical_host_->locality();
  }
  Stats::StatName localityZoneStatName() const override {
    return logical_host_->localityZoneStatName();
  }
  Network::Address::InstanceConstSharedPtr healthCheckAddress() const override {
    // Should never be called since real hosts are used only for forwarding and not health
    // checking.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  uint32_t priority() const override { return logical_host_->priority(); }
  void priority(uint32_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

private:
  const Network::Address::InstanceConstSharedPtr address_;
  const HostConstSharedPtr logical_host_;
};

} // namespace Upstream
} // namespace Envoy
