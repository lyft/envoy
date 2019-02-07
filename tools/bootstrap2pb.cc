/**
 * Utility to convert bootstrap from its YAML/JSON/proto representation to text
 * proto.
 *
 * Usage:
 *
 * bootstrap2pb <input YAML/JSON/proto path> <output text proto path>
 */
#include <cstdlib>
#include <fstream>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

#include "common/api/api_impl.h"
#include "common/common/assert.h"
#include "common/event/real_time_system.h"
#include "common/protobuf/utility.h"

#include "exe/platform_impl.h"

// NOLINT(namespace-envoy)
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input YAML/JSON/proto path> <output text text path>"
              << std::endl;
    return EXIT_FAILURE;
  }

  Envoy::PlatformImpl platform_impl_;
  Envoy::Event::RealTimeSystem time_system; // NO_CHECK_FORMAT(real_time)
  Envoy::Api::Impl api(platform_impl_.threadFactory(), time_system);

  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  Envoy::MessageUtil::loadFromFile(argv[1], bootstrap, api);
  std::ofstream bootstrap_file(argv[2]);
  bootstrap_file << bootstrap.DebugString();
  return EXIT_SUCCESS;
}
