#include "test/test_common/simulated_time_system.h"

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/real_time_system.h"
#include "common/event/timer_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

// Our simulated alarm inherits from TimerImpl so that the same dispatching
// mechanism used in RealTimeSystem timers is employed for simulated alarms.
// using libevent's event_active(). Note that libevent is placed into
// thread-safe mode due to the call to evthread_use_pthreads() in
// source/common/event/libevent.cc.
class SimulatedTimeSystem::Alarm : public TimerImpl {
public:
  Alarm(SimulatedTimeSystem& time_system, Libevent::BasePtr& libevent, TimerCb cb)
      : TimerImpl(libevent, cb), time_system_(time_system), libevent_(libevent),
        index_(time_system.nextIndex()), armed_(false) {}

  virtual ~Alarm();

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& duration) override;

  void setTime(MonotonicTime time) { time_ = time; }
  void run() {
    armed_ = false;
    std::chrono::milliseconds duration = std::chrono::milliseconds::zero();
    TimerImpl::enableTimer(duration);

    // Proper locking occurs in the libevent library, but it will issue
    // a benign warning if there's another concurrent loop running:
    // https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/event.c#L1920
    event_base_loop(libevent_.get(), EVLOOP_NONBLOCK);
  }
  MonotonicTime time() const {
    ASSERT(armed_);
    return time_;
  }
  uint64_t index() const { return index_; }

private:
  SimulatedTimeSystem& time_system_;
  Libevent::BasePtr& libevent_;
  MonotonicTime time_;
  uint64_t index_;
  bool armed_;
};

// Compare two alarms, based on wakeup time and insertion order. Returns true if
// a comes before b.

// like strcmp (<0 for a < b, >0 for a > b), based on wakeup time and index.
bool SimulatedTimeSystem::CompareAlarms::operator()(const Alarm* a, const Alarm* b) const {
  if (a != b) {
    if (a->time() < b->time()) {
      return true;
    } else if (a->time() == b->time() && a->index() < b->index()) {
      return true;
    }
  }
  return false;
};

// Each scheduler maintains its own timer
class SimulatedTimeSystem::SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystem& time_system, Libevent::BasePtr& libevent)
      : time_system_(time_system), libevent_(libevent) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<SimulatedTimeSystem::Alarm>(time_system_, libevent_, cb);
  };

private:
  SimulatedTimeSystem& time_system_;
  Libevent::BasePtr& libevent_;
};

SimulatedTimeSystem::Alarm::~Alarm() {
  if (armed_) {
    disableTimer();
  }
}

void SimulatedTimeSystem::Alarm::disableTimer() {
  ASSERT(armed_);
  time_system_.removeAlarm(this);
  armed_ = false;
}

void SimulatedTimeSystem::Alarm::enableTimer(const std::chrono::milliseconds& duration) {
  ASSERT(!armed_);
  armed_ = true;
  if (duration.count() == 0) {
    run();
  } else {
    time_system_.addAlarm(this, duration);
  }
}

// When we initialize our simulated time, we'll start the current time based on
// the real current time. But thereafter, real-time will not be used, and time
// will march forward only by calling sleep().
SimulatedTimeSystem::SimulatedTimeSystem()
    : monotonic_time_(real_time_source_.monotonicTime()),
      system_time_(real_time_source_.systemTime()), index_(0) {}

SystemTime SimulatedTimeSystem::systemTime() {
  Thread::LockGuard lock(mutex_);
  return system_time_;
}

MonotonicTime SimulatedTimeSystem::monotonicTime() {
  Thread::LockGuard lock(mutex_);
  return monotonic_time_;
}

int64_t SimulatedTimeSystem::nextIndex() {
  Thread::LockGuard lock(mutex_);
  return index_++;
}

void SimulatedTimeSystem::addAlarm(Alarm* alarm, const std::chrono::milliseconds& duration) {
  Thread::LockGuard lock(mutex_);
  alarm->setTime(monotonic_time_ + duration);
  alarms_.insert(alarm);
}

void SimulatedTimeSystem::removeAlarm(Alarm* alarm) {
  Thread::LockGuard lock(mutex_);
  alarms_.erase(alarm);
}

SchedulerPtr SimulatedTimeSystem::createScheduler(Libevent::BasePtr& libevent) {
  return std::make_unique<SimulatedScheduler>(*this, libevent);
}

void SimulatedTimeSystem::setMonotonicTimeAndUnlock(const MonotonicTime& monotonic_time) {
  // We don't have a convenient LockGuard construct that allows temporarily
  // dropping the lock to run a callback. The main issue here is that we must
  // be careful not to be holding mutex_ when an exception can be thrown.
  // That can only happen here in alarm->run(), which is run with the mutex
  // released.
  if (monotonic_time >= monotonic_time_) {
    while (!alarms_.empty()) {
      AlarmSet::iterator pos = alarms_.begin();
      Alarm* alarm = *pos;
      if (alarm->time() > monotonic_time) {
        break;
      }
      ASSERT(alarm->time() >= monotonic_time_);
      system_time_ +=
          std::chrono::duration_cast<SystemTime::duration>(alarm->time() - monotonic_time_);
      monotonic_time_ = alarm->time();
      alarms_.erase(pos);
      mutex_.unlock();
      alarm->run(); // Might throw, exiting function with lock dropped, which is acceptable.
      mutex_.lock();
    }
    system_time_ +=
        std::chrono::duration_cast<SystemTime::duration>(monotonic_time - monotonic_time_);
    monotonic_time_ = monotonic_time;
  }
  mutex_.unlock();
}

void SimulatedTimeSystem::setSystemTime(const SystemTime& system_time) {
  mutex_.lock();
  if (system_time > system_time_) {
    MonotonicTime monotonic_time =
        monotonic_time_ +
        std::chrono::duration_cast<MonotonicTime::duration>(system_time - system_time_);
    setMonotonicTimeAndUnlock(monotonic_time);
  } else {
    system_time_ = system_time;
    mutex_.unlock();
  }
}

} // namespace Event
} // namespace Envoy
