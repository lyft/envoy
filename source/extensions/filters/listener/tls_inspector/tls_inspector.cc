#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"

#include "absl/strings/str_join.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

// Min/max TLS version recognized by the underlying TLS/SSL library.
const unsigned Config::TLS_MIN_SUPPORTED_VERSION = TLS1_VERSION;
const unsigned Config::TLS_MAX_SUPPORTED_VERSION = TLS1_3_VERSION;

Config::Config(Stats::Scope& scope, uint32_t max_client_hello_size)
    : stats_{ALL_TLS_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "tls_inspector."))},
      ssl_ctx_(SSL_CTX_new(TLS_with_buffers_method())),
      max_client_hello_size_(max_client_hello_size) {

  if (max_client_hello_size_ > TLS_MAX_CLIENT_HELLO) {
    throw EnvoyException(fmt::format("max_client_hello_size of {} is greater than maximum of {}.",
                                     max_client_hello_size_, size_t(TLS_MAX_CLIENT_HELLO)));
  }

  SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS_MIN_SUPPORTED_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS_MAX_SUPPORTED_VERSION);
  SSL_CTX_set_options(ssl_ctx_.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_select_certificate_cb(
      ssl_ctx_.get(), [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
        const uint8_t* data;
        size_t len;
        if (SSL_early_callback_ctx_extension_get(
                client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &len)) {
          Filter* filter = static_cast<Filter*>(SSL_get_app_data(client_hello->ssl));
          filter->onALPN(data, len);
        }
        return ssl_select_cert_success;
      });
  SSL_CTX_set_tlsext_servername_callback(
      ssl_ctx_.get(), [](SSL* ssl, int* out_alert, void*) -> int {
        Filter* filter = static_cast<Filter*>(SSL_get_app_data(ssl));
        filter->onServername(
            absl::NullSafeStringView(SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name)));

        // Return an error to stop the handshake; we have what we wanted already.
        *out_alert = SSL_AD_USER_CANCELLED;
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      });
}

bssl::UniquePtr<SSL> Config::newSsl() { return bssl::UniquePtr<SSL>{SSL_new(ssl_ctx_.get())}; }

Filter::Filter(const ConfigSharedPtr config) : config_(config), ssl_(config_->newSsl()) {
  SSL_set_app_data(ssl_.get(), this);
  SSL_set_accept_state(ssl_.get());
}

Network::FilterStatus Filter::onInspectData(Buffer::Instance& buffer) {
  if (buffer.length() > read_) {
    // Although the underlay using single slice, but the filter shouldn't assume
    // the underlay implementation. So using the linearize to get a continuous
    // memory space.
    const uint8_t* data = static_cast<uint8_t*>(buffer.linearize(buffer.length())) + read_;
    const size_t len = buffer.length() - read_;
    read_ = buffer.length();
    ParseState parse_state = parseClientHello(data, len);
    switch (parse_state) {
    case ParseState::Error:
      cb_->socket().ioHandle().close();
      return Network::FilterStatus::StopIteration;
    case ParseState::Done:
      // finish the inspect
      return Network::FilterStatus::Continue;
    case ParseState::Continue:
      // do nothing but wait for the next event
      return Network::FilterStatus::StopIteration;
    }
  }
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "tls inspector: new connection accepted");
  cb_ = &cb;
  // waiting for the inspect data.
  return Network::FilterStatus::StopIteration;
}

void Filter::onALPN(const unsigned char* data, unsigned int len) {
  CBS wire, list;
  CBS_init(&wire, reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(len));
  if (!CBS_get_u16_length_prefixed(&wire, &list) || CBS_len(&wire) != 0 || CBS_len(&list) < 2) {
    // Don't produce errors, let the real TLS stack do it.
    return;
  }
  CBS name;
  std::vector<absl::string_view> protocols;
  while (CBS_len(&list) > 0) {
    if (!CBS_get_u8_length_prefixed(&list, &name) || CBS_len(&name) == 0) {
      // Don't produce errors, let the real TLS stack do it.
      return;
    }
    protocols.emplace_back(reinterpret_cast<const char*>(CBS_data(&name)), CBS_len(&name));
  }
  ENVOY_LOG(trace, "tls:onALPN(), ALPN: {}", absl::StrJoin(protocols, ","));
  cb_->socket().setRequestedApplicationProtocols(protocols);
  alpn_found_ = true;
}

void Filter::onServername(absl::string_view name) {
  if (!name.empty()) {
    config_->stats().sni_found_.inc();
    cb_->socket().setRequestedServerName(name);
    ENVOY_LOG(debug, "tls:onServerName(), requestedServerName: {}", name);
  } else {
    config_->stats().sni_not_found_.inc();
  }
  clienthello_success_ = true;
}

ParseState Filter::parseClientHello(const void* data, size_t len) {
  // Ownership is passed to ssl_ in SSL_set_bio()
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(data, len));

  // Make the mem-BIO return that there is more data
  // available beyond it's end
  BIO_set_mem_eof_return(bio.get(), -1);

  SSL_set_bio(ssl_.get(), bio.get(), bio.get());
  bio.release();

  int ret = SSL_do_handshake(ssl_.get());

  // This should never succeed because an error is always returned from the SNI callback.
  ASSERT(ret <= 0);
  switch (SSL_get_error(ssl_.get(), ret)) {
  case SSL_ERROR_WANT_READ:
    if (read_ == config_->maxClientHelloSize()) {
      // We've hit the specified size limit. This is an unreasonably large ClientHello;
      // indicate failure.
      config_->stats().client_hello_too_large_.inc();
      return ParseState::Error;
    }
    return ParseState::Continue;
  case SSL_ERROR_SSL:
    if (clienthello_success_) {
      config_->stats().tls_found_.inc();
      if (alpn_found_) {
        config_->stats().alpn_found_.inc();
      } else {
        config_->stats().alpn_not_found_.inc();
      }
      cb_->socket().setDetectedTransportProtocol("tls");
    } else {
      config_->stats().tls_not_found_.inc();
    }
    return ParseState::Done;
  default:
    return ParseState::Error;
  }
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
