#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

void ListenerFilterFuzzer::fuzz(
    Network::ListenerFilter& filter,
    const test::extensions::filters::listener::FilterFuzzTestCase& input) {
  try {
    socket_.setLocalAddress(Network::Utility::resolveUrl(input.sock().local_address()));
  } catch (const EnvoyException& e) {
    // Socket's local address will be nullptr by default if fuzzed local address is malformed
    // or missing - local address field in proto is optional
  }
  try {
    socket_.setRemoteAddress(Network::Utility::resolveUrl(input.sock().remote_address()));
  } catch (const EnvoyException& e) {
    // Socket's remote address will be nullptr by default if fuzzed remote address is malformed
    // or missing - remote address field in proto is optional
  }

  FuzzedHeader header(input);

  if (!header.empty()) {
    ON_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, MSG_PEEK))
        .WillByDefault(testing::Return(Api::SysCallSizeResult{static_cast<ssize_t>(0), 0}));

    ON_CALL(dispatcher_,
            createFileEvent_(_, _, Event::FileTriggerType::Edge,
                             Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillByDefault(testing::DoAll(testing::SaveArg<1>(&file_event_callback_),
                                      testing::ReturnNew<NiceMock<Event::MockFileEvent>>()));
  }

  filter.onAccept(cb_);

  if (file_event_callback_ == nullptr) {
    // If filter does not call createFileEvent (i.e. original_dst and original_src)
    return;
  }

  if (!header.empty()) {
    {
      testing::InSequence s;

      EXPECT_CALL(os_sys_calls_, recv(kFakeSocketFd, _, _, MSG_PEEK))
          .Times(testing::AnyNumber())
          .WillRepeatedly(Invoke([&header](os_fd_t, void* buffer, size_t length, int)
                                 -> Api::SysCallSizeResult {
            return header.next(buffer, length);
          }));
    }

    bool got_continue = false;

    ON_CALL(cb_, continueFilterChain(true))
        .WillByDefault(testing::InvokeWithoutArgs([&got_continue]() { got_continue = true; }));

    while (!got_continue) {
      if (header.done()) { // End of stream reached but not done
        file_event_callback_(Event::FileReadyType::Closed);
      } else {
        file_event_callback_(Event::FileReadyType::Read);
      }
    }
  }
}

FuzzedHeader::FuzzedHeader(const test::extensions::filters::listener::FilterFuzzTestCase& input)
    : nreads(input.data_size()), nread(0), header("") {
  for (int i = 0; i < nreads; i++) {
    header += input.data(i);
    indices.push_back(header.size());
  }
}

Api::SysCallSizeResult FuzzedHeader::next(void* buffer, size_t length) {
  if (done()) {         // End of stream reached
    nread = nreads - 1; // Decrement to avoid out-of-range for last recv() call
  }
  ASSERT(length >= indices[nread]);
  memcpy(buffer, header.data(), indices[nread]);
  return Api::SysCallSizeResult{ssize_t(indices[nread++]), 0};
}

bool FuzzedHeader::done() { return (nread >= nreads); }

bool FuzzedHeader::empty() { return (nreads == 0); }

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
