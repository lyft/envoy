#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"

namespace Envoy {
namespace Quic {

// QuicHttpConnectionImplBase instance is a thin QUIC codec just providing quic interface to HCM.
// Owned by HCM and created during onNewConnection() if the network connection
// is a QUIC connection.
class QuicHttpConnectionImplBase : public virtual Http::Connection,
                                   protected Logger::Loggable<Logger::Id::quic> {
public:
  QuicHttpConnectionImplBase(quic::QuicSpdySession& quic_session) : quic_session_(quic_session) {}

  // Http::Connection
  void dispatch(Buffer::Instance& /*data*/) override {
    // Bypassed. QUIC connection already hands all data to streams.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  Http::Protocol protocol() override { return Http::Protocol::Http3; }
  // Returns true if the session has data to send but queued in connection or
  // stream send buffer.
  bool wantsToWrite() override;
  void onUnderlyingConnectionAboveWriteBufferHighWatermark() override;
  void onUnderlyingConnectionBelowWriteBufferLowWatermark() override;

protected:
  quic::QuicSpdySession& quic_session_;
};

class QuicHttpServerConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ServerConnection {
public:
  QuicHttpServerConnectionImpl(EnvoyQuicServerSession& quic_session,
                               Http::ServerConnectionCallbacks& callbacks)
      : QuicHttpConnectionImplBase(quic_session), quic_server_session_(quic_session) {
    quic_session.setHttpConnectionCallbacks(callbacks);
  }

  // Http::Connection
  void goAway() override;
  void shutdownNotice() override {
    // TODO(danzh): Add double-GOAWAY support in QUIC.
    ENVOY_CONN_LOG(error, "Shutdown notice is not propagated to QUIC.", quic_server_session_);
  }

private:
  EnvoyQuicServerSession& quic_server_session_;
};

class QuicHttpClientConnectionImpl : public QuicHttpConnectionImplBase,
                                     public Http::ClientConnection {
public:
  QuicHttpClientConnectionImpl(EnvoyQuicClientSession& session,
                               Http::ConnectionCallbacks& callbacks)
      : QuicHttpConnectionImplBase(session), quic_session_(session) {
    quic_session_.setHttpConnectionCallbacks(callbacks);
  }

  // Http::ClientConnection
  Http::StreamEncoder& newStream(Http::StreamDecoder& response_decoder) override;

  // Http::Connection
  void goAway() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void shutdownNotice() override { NOT_REACHED_GCOVR_EXCL_LINE; }

private:
  EnvoyQuicClientSession& quic_session_;
};

} // namespace Quic
} // namespace Envoy
