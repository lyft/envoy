#include "server/active_raw_udp_listener_config.h"

#include <memory>
#include <string>

#include "envoy/config/listener/v3/listener.pb.h"

#include "server/active_udp_listener.h"
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

ActiveRawUdpListenerFactory::ActiveRawUdpListenerFactory(uint32_t concurrency)
    : concurrency_(concurrency) {}

Network::ConnectionHandler::ActiveUdpListenerPtr
ActiveRawUdpListenerFactory::createActiveUdpListener(uint32_t worker_index,
                                                     Network::UdpConnectionHandler& parent,
                                                     Event::Dispatcher& dispatcher,
                                                     Network::ListenerConfig& config) {
  return std::make_unique<ActiveRawUdpListener>(worker_index, concurrency_, parent, dispatcher,
                                                config);
}

ProtobufTypes::MessagePtr ActiveRawUdpListenerConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::listener::v3::ActiveRawUdpListenerConfig>();
}

Network::ActiveUdpListenerFactoryPtr
ActiveRawUdpListenerConfigFactory::createActiveUdpListenerFactory(
    const Protobuf::Message& /*message*/, uint32_t concurrency) {
  return std::make_unique<Server::ActiveRawUdpListenerFactory>(concurrency);
}

std::string ActiveRawUdpListenerConfigFactory::name() const { return "default_udp_listener"; }

REGISTER_FACTORY(ActiveRawUdpListenerConfigFactory, Server::ActiveUdpListenerConfigFactory);

} // namespace Server
} // namespace Envoy
