#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier_base.h"

namespace Envoy {
namespace Quic {

class TestProofVerifier : public EnvoyQuicProofVerifierBase {
public:
  quic::QuicAsyncStatus
  VerifyCertChain(const std::string& /*hostname*/, const uint16_t /*port*/,
                  const std::vector<std::string>& /*certs*/, const std::string& /*ocsp_response*/,
                  const std::string& /*cert_sct*/, const quic::ProofVerifyContext* /*context*/,
                  std::string* /*error_details*/,
                  std::unique_ptr<quic::ProofVerifyDetails>* /*details*/,
                  std::unique_ptr<quic::ProofVerifierCallback> /*callback*/) override {
    return quic::QUIC_SUCCESS;
  }

  bool verifySignature(const std::string& /*server_config*/, absl::string_view /*chlo_hash*/,
                       const std::string& /*cert*/, const std::string& /*signature*/) override {
    return true;
  }
};

} // namespace Quic
} // namespace Envoy
