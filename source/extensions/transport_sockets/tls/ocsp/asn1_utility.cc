#include "extensions/transport_sockets/tls/ocsp/asn1_utility.h"
#include "common/common/c_smart_ptr.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

namespace {
  // A type adapter since OPENSSL_free accepts void*.
  void freeOpensslString(char* str) {
    OPENSSL_free(str);
  }

  // ASN1_INTEGER is a type alias for ASN1_STRING.
  // This static_cast is intentional to avoid the
  // c-style cast performed in M_ASN1_INTEGER_free.
  void freeAsn1Integer(ASN1_INTEGER* integer) {
    ASN1_STRING_free(static_cast<ASN1_STRING*>(integer));
  }
}

absl::string_view Asn1Utility::cbsToString(CBS& cbs) {
  auto str_head = reinterpret_cast<const char*>(CBS_data(&cbs));
  return {str_head, CBS_len(&cbs)};
}

bool Asn1Utility::getOptional(CBS& cbs, CBS* data, unsigned tag) {
  int is_present;
  if (!CBS_get_optional_asn1(&cbs, data, &is_present, tag)) {
    throw Envoy::EnvoyException("Failed to parse ASN.1 element tag");
  }

  return is_present;
}

std::string Asn1Utility::parseOid(CBS& cbs) {
  CBS oid;
  if (!CBS_get_asn1(&cbs, &oid, CBS_ASN1_OBJECT)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 OBJECT");
  }
  CSmartPtr<char, freeOpensslString> oid_text(CBS_asn1_oid_to_text(&oid));
  if (oid_text == nullptr) {
    throw Envoy::EnvoyException("Failed to parse oid");
  }

  std::string oid_text_str(oid_text.get());
  return oid_text_str;
}

Envoy::SystemTime Asn1Utility::parseGeneralizedTime(CBS& cbs) {
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_GENERALIZEDTIME)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 GENERALIZEDTIME");
  }

  auto time_str = cbsToString(elem);
  // OCSP follows the RFC 5280 enforcement that GENERALIZEDTIME
  // fields MUST be in UTC, so must be suffixed with a Z character.
  // Local time or time differential, though a part of the ASN.1
  // GENERALIZEDTIME spec, are not supported.
  // Reference: https://tools.ietf.org/html/rfc5280#section-4.1.2.5.2
  if (absl::ascii_toupper(time_str.at(time_str.length() - 1)) != 'Z') {
    throw Envoy::EnvoyException("GENERALIZEDTIME must be in UTC");
  }

  absl::Time time;
  auto utc_time_str = time_str.substr(0, time_str.length() - 1);
  std::string parse_error;
  if (!absl::ParseTime(GENERALIZED_TIME_FORMAT, utc_time_str, &time, &parse_error)) {
    throw Envoy::EnvoyException(absl::StrCat("Error parsing timestamp ", time_str, " with format ",
                                             GENERALIZED_TIME_FORMAT, ". Error: ", parse_error));
  }
  return absl::ToChronoTime(time);
}

// Performs the following conversions to go from bytestring to hex integer
// CBS -> ASN1_INTEGER -> BIGNUM -> String
std::string Asn1Utility::parseInteger(CBS& cbs) {
  CBS num;
  if (!CBS_get_asn1(&cbs, &num, CBS_ASN1_INTEGER)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 INTEGER");
  }

  auto head = CBS_data(&num);
  CSmartPtr<ASN1_INTEGER, freeAsn1Integer> asn1_integer(c2i_ASN1_INTEGER(nullptr, &head, CBS_len(&num)));
  if (asn1_integer != nullptr) {
    BIGNUM num_bn;
    BN_init(&num_bn);
    ASN1_INTEGER_to_BN(asn1_integer.get(), &num_bn);

    CSmartPtr<char, freeOpensslString> char_hex_number(BN_bn2hex(&num_bn));
    BN_free(&num_bn);
    if (char_hex_number != nullptr) {
      std::string hex_number(char_hex_number.get());
      return hex_number;
    }
  }

  throw Envoy::EnvoyException("Failed to parse ASN.1 INTEGER");
}

std::vector<uint8_t> Asn1Utility::parseOctetString(CBS& cbs) {
  CBS value;
  if (!CBS_get_asn1(&cbs, &value, CBS_ASN1_OCTETSTRING)) {
    throw Envoy::EnvoyException("Input is not a well-formed ASN.1 OCTETSTRING");
  }

  auto data = reinterpret_cast<const uint8_t*>(CBS_data(&value));
  return {data, data + CBS_len(&value)};
}

std::string Asn1Utility::parseAlgorithmIdentifier(CBS& cbs) {
  // AlgorithmIdentifier  ::=  SEQUENCE  {
  //    algorithm               OBJECT IDENTIFIER,
  //    parameters              ANY DEFINED BY algorithm OPTIONAL
  // }
  CBS elem;
  if (!CBS_get_asn1(&cbs, &elem, CBS_ASN1_SEQUENCE)) {
    throw Envoy::EnvoyException("AlgorithmIdentifier is not a well-formed ASN.1 SEQUENCE");
  }

  return parseOid(elem);
  // Ignore `parameters`.
}

void Asn1Utility::skipOptional(CBS& cbs, unsigned tag) {
  if (!CBS_get_optional_asn1(&cbs, nullptr, nullptr, tag)) {
    throw Envoy::EnvoyException("Failed to parse ASN.1 element tag");
  }
}

void Asn1Utility::skip(CBS& cbs, unsigned tag) {
  if (!CBS_get_asn1(&cbs, nullptr, tag)) {
    throw Envoy::EnvoyException("Failed to parse ASN.1 element");
  }
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
