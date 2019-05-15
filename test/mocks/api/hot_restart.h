#pragma once

#include "envoy/api/os_sys_calls.h"

#include "common/api/os_sys_calls_impl_hot_restart.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Api {

class MockHotRestartOsSysCalls : public HotRestartOsSysCallsImpl {
public:
  // Api::HotRestartOsSysCalls
  MOCK_METHOD3(shmOpen, SysCallIntResult(const char*, int, mode_t));
  MOCK_METHOD1(shmUnlink, SysCallIntResult(const char*));
};

} // namespace Api
} // namespace Envoy
