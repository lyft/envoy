#include <string>
#include <vector>

#include "extensions/transport_sockets/tls/utility.h"

#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/extensions/transport_sockets/tls/test_data/san_dns_cert_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

TEST_F(TestBase, UtilityTest_TestGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST_F(TestBase, UtilityTest_TestMultipleGetSubjectAlternateNamesWithDNS) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/transport_sockets/tls/test_data/san_multiple_dns_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_DNS);
  EXPECT_EQ(2, subject_alt_names.size());
}

TEST_F(TestBase, UtilityTest_TestGetSubjectAlternateNamesWithUri) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"));
  const std::vector<std::string>& subject_alt_names = Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(1, subject_alt_names.size());
}

TEST_F(TestBase, UtilityTest_TestGetSubjectAlternateNamesWithNoSAN) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/no_san_cert.pem"));
  const std::vector<std::string>& uri_subject_alt_names =
      Utility::getSubjectAltNames(*cert, GEN_URI);
  EXPECT_EQ(0, uri_subject_alt_names.size());
}

TEST_F(TestBase, UtilityTest_TestGetSubject) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ("CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US",
            Utility::getSubjectFromCertificate(*cert));
}

TEST_F(TestBase, UtilityTest_TestGetSerialNumber) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  EXPECT_EQ(TEST_SAN_DNS_CERT_SERIAL, Utility::getSerialNumberFromCertificate(*cert));
}

TEST_F(TestBase, UtilityTest_TestDaysUntilExpiration) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  // Set a known date (2033-05-18 03:33:20 UTC) so that we get fixed output from this test.
  const time_t known_date_time = 2000000000;
  Event::SimulatedTimeSystem time_source;
  time_source.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));

  // Get expiration time from the certificate info.
  const absl::Time expiration =
      TestUtility::parseTime(TEST_SAN_DNS_CERT_NOT_AFTER, "%b %e %H:%M:%S %Y GMT");

  int days = std::difftime(absl::ToTimeT(expiration), known_date_time) / (60 * 60 * 24);
  EXPECT_EQ(days, Utility::getDaysUntilExpiration(cert.get(), time_source));
}

TEST_F(TestBase, UtilityTest_TestDaysUntilExpirationWithNull) {
  Event::SimulatedTimeSystem time_source;
  EXPECT_EQ(std::numeric_limits<int>::max(), Utility::getDaysUntilExpiration(nullptr, time_source));
}

TEST_F(TestBase, UtilityTest_TestValidFrom) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getValidFrom(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_BEFORE, formatted);
}

TEST_F(TestBase, UtilityTest_TestExpirationTime) {
  bssl::UniquePtr<X509> cert = readCertFromFile(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  const std::string formatted =
      TestUtility::formatTime(Utility::getExpirationTime(*cert), "%b %e %H:%M:%S %Y GMT");
  EXPECT_EQ(TEST_SAN_DNS_CERT_NOT_AFTER, formatted);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
