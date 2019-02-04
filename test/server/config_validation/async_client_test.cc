#include "envoy/http/message.h"

#include "common/http/message_impl.h"

#include "server/config_validation/async_client.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace Http {

TEST_F(TestBase, ValidationAsyncClientTest_MockedMethods) {
  MessagePtr message{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks;
  MockAsyncClientStreamCallbacks stream_callbacks;

  DangerousDeprecatedTestTime test_time;
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  ValidationAsyncClient client(test_time.timeSystem(), *api);
  EXPECT_EQ(nullptr, client.send(std::move(message), callbacks, AsyncClient::RequestOptions()));
  EXPECT_EQ(nullptr, client.start(stream_callbacks, AsyncClient::StreamOptions()));
}

} // namespace Http
} // namespace Envoy
