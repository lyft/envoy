#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"

#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api::Api {
public:
  Impl(std::chrono::milliseconds file_flush_interval_msec, Stats::Store& stats_store);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override;
  Filesystem::FileSharedPtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                       Thread::BasicLockable& lock) override;
  bool fileExists(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;

private:
  Filesystem::Instance file_system_;
};

} // namespace Api
} // namespace Envoy
