#include "envoy/network/filter.h"

#include "test/extensions/filters/listener/common/listener_filter_fuzz_test.pb.validate.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/fakes.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

class UberFilterFuzzer {
public:
  void fuzz(Network::ListenerFilter& filter,
            const test::extensions::filters::listener::FilterFuzzTestCase& input);

private:
  void fuzzerSetup(const test::extensions::filters::listener::FilterFuzzTestCase& input) {
    ON_CALL(cb_, socket()).WillByDefault(testing::ReturnRef(socket_));
    socketSetup(input);
  }

  void socketSetup(const test::extensions::filters::listener::FilterFuzzTestCase& input);

  NiceMock<Network::MockListenerFilterCallbacks> cb_;
  Network::FakeConnectionSocket socket_;
  // NiceMock<Event::MockDispatcher> dispatcher_;
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
