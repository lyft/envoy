#include "envoy/event/dispatcher.h"

#include "common/network/utility.h"

#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"

namespace Envoy {
namespace Quic {

// A client QuicConnection instance manages its own I/O events.
class EnvoyQuicClientConnection : public EnvoyQuicConnection, public Network::UdpPacketProcessor {
public:
  // A connection socket will be created with given |local_addr|. If binding
  // port not provided in |local_addr|, pick up a random port.
  EnvoyQuicClientConnection(quic::QuicConnectionId server_connection_id,
                            Network::Address::InstanceConstSharedPtr& initial_peer_address,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Network::Address::InstanceConstSharedPtr local_addr,
                            Event::Dispatcher& dispatcher);

  // Overridden to un-register all file events.
  ~EnvoyQuicClientConnection() override;

  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time) override;

  uint64_t maxPacketSize() const override;

private:
  EnvoyQuicClientConnection(quic::QuicConnectionId server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket);

  EnvoyQuicClientConnection(quic::QuicConnectionId server_connection_id,
                            quic::QuicConnectionHelperInterface& helper,
                            quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                            bool owns_writer,
                            const quic::ParsedQuicVersionVector& supported_versions,
                            Event::Dispatcher& dispatcher,
                            Network::ConnectionSocketPtr&& connection_socket);

  Network::ConnectionSocketPtr
  createConnectionSocket(Network::Address::InstanceConstSharedPtr& peer_addr,
                         Network::Address::InstanceConstSharedPtr& local_addr);

  void onFileEvent(uint32_t events);
  uint32_t packets_dropped_{0};
  Event::Dispatcher& dispatcher_;
  Event::FileEventPtr file_event_;
};

} // namespace Quic
} // namespace Envoy
