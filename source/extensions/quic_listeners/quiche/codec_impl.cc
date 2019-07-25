#include "extensions/quic_listeners/quiche/codec_impl.h"

namespace Envoy {
namespace Quic {

void QuicHttpConnectionImplBase::goAway() {
  quic_session_.SendGoAway(quic::QUIC_PEER_GOING_AWAY, "server shutdown imminent");
}

bool QuicHttpConnectionImplBase::wantsToWrite() {
  // Returns true if the session has data to send but queued in connection or
  // stream send buffer.
  return quic_session_.HasDataToWrite();
}

} // namespace Quic
} // namespace Envoy
