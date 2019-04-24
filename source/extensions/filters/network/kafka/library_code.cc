#include <memory>

#include "extensions/filters/network/kafka/generated.h"
#include "extensions/filters/network/kafka/library_header.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

int Runner::doSomething() {
  Argument arg;
  const std::unique_ptr<Something2> ptr = std::make_unique<Something2>();
  return ptr->someMethod(arg);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
