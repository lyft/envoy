#include "test/test_common/simulated_time_system.h"

#include <chrono>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/event/real_time_system.h"
#include "common/event/timer_impl.h"

namespace Envoy {
namespace Event {

// Our simulated alarm inherits from TimerImpl so that the same dispatching
// mechanism used in RealTimeSystem timers is employed for simulated alarms.
class SimulatedTimeSystemHelper::Alarm : public Timer {
public:
  Alarm(SimulatedTimeSystemHelper& time_system, Scheduler& base_scheduler, TimerCb cb)
      : base_timer_(base_scheduler.createTimer([this, cb] { runAlarm(cb); })),
        time_system_(time_system), index_(time_system.nextIndex()), armed_(false) {}

  virtual ~Alarm();

  // Timer
  void disableTimer() override;
  void enableTimer(const std::chrono::milliseconds& duration) override;
  bool enabled() override {
    Thread::LockGuard lock(time_system_.mutex_);
    return armed_;
  }

  void disableTimerLockHeld() EXCLUSIVE_LOCKS_REQUIRED(time_system_.mutex_);

  void setTimeLockHeld(MonotonicTime time) { // EXCLUSIVE_LOCKS_REQUIRED(time_system_.mutex_)
    time_ = time;
  }

  /**
   * Activates the timer so it will be run the next time the libevent loop is run,
   * typically via Dispatcher::run().
   */
  void activateLockHeld() NO_THREAD_SAFETY_ANALYSIS {
    ASSERT(armed_);
    armed_ = false;
    time_system_.incPending();

    // We don't want to activate the alarm under lock, as it will make a libevent call,
    // and libevent itself uses locks:
    // https://github.com/libevent/libevent/blob/29cc8386a2f7911eaa9336692a2c5544d8b4734f/event.c#L1917
    time_system_.mutex_.unlock();
    std::chrono::milliseconds duration = std::chrono::milliseconds::zero();
    base_timer_->enableTimer(duration);
    time_system_.mutex_.lock();
  }

  MonotonicTime time() const { //  EXCLUSIVE_LOCKS_REQUIRED(time_system_.mutex_)
    ASSERT(armed_);
    return time_;
  }

  uint64_t index() const { return index_; }

private:
  void runAlarm(TimerCb cb) {
    cb();
    time_system_.decPending();
  }

  TimerPtr base_timer_;
  SimulatedTimeSystemHelper& time_system_;
  MonotonicTime time_; // GUARDED_BY(time_system_.mutex_);
  const uint64_t index_;
  bool armed_; // GUARDED_BY(time_system_.mutex_);
};

// Compare two alarms, based on wakeup time and insertion order. Returns true if
// a comes before b.
bool SimulatedTimeSystemHelper::CompareAlarms::operator()(const Alarm* a, const Alarm* b) const {
  if (a != b) {
    if (a->time() < b->time()) {
      return true;
    } else if (a->time() == b->time() && a->index() < b->index()) {
      return true;
    }
  }
  return false;
};

// Each timer is maintained and ordered by a common TimeSystem, but is
// associated with a scheduler. The scheduler creates the timers with a libevent
// context, so that the timer callbacks can be executed via Dispatcher::run() in
// the expected thread.
class SimulatedTimeSystemHelper::SimulatedScheduler : public Scheduler {
public:
  SimulatedScheduler(SimulatedTimeSystemHelper& time_system, Scheduler& base_scheduler)
      : time_system_(time_system), base_scheduler_(base_scheduler) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<SimulatedTimeSystemHelper::Alarm>(time_system_, base_scheduler_, cb);
  };

private:
  SimulatedTimeSystemHelper& time_system_;
  Scheduler& base_scheduler_;
};

SimulatedTimeSystemHelper::Alarm::Alarm::~Alarm() {
  if (armed_) {
    disableTimer();
  }
}

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimer() {
  Thread::LockGuard lock(time_system_.mutex_);
  disableTimerLockHeld();
}

void SimulatedTimeSystemHelper::Alarm::Alarm::disableTimerLockHeld() {
  if (armed_) {
    time_system_.removeAlarmLockHeld(this);
    armed_ = false;
  }
}

void SimulatedTimeSystemHelper::Alarm::Alarm::enableTimer(
    const std::chrono::milliseconds& duration) {
  Thread::LockGuard lock(time_system_.mutex_);
  disableTimerLockHeld();
  armed_ = true;
  if (duration.count() == 0) {
    activateLockHeld();
  } else {
    time_system_.addAlarmLockHeld(this, duration);
  }
}

// It would be very confusing if there were more than one simulated time system
// extant at once. Technically this might be something we want, but more likely
// it indicates some kind of plumbing error in test infrastructure. So track
// the instance count with a simple int. In the future if there's a good reason
// to have more than one around at a time, this variable can be deleted.
static int instance_count = 0;

// When we initialize our simulated time, we'll start the current time based on
// the real current time. But thereafter, real-time will not be used, and time
// will march forward only by calling sleep().
SimulatedTimeSystemHelper::SimulatedTimeSystemHelper()
    : monotonic_time_(MonotonicTime(std::chrono::seconds(0))),
      system_time_(real_time_source_.systemTime()), index_(0), pending_alarms_(0) {
  ++instance_count;
  ASSERT(instance_count <= 1);
}

SimulatedTimeSystemHelper::~SimulatedTimeSystemHelper() { --instance_count; }

bool SimulatedTimeSystemHelper::hasInstance() { return instance_count > 0; }

SystemTime SimulatedTimeSystemHelper::systemTime() {
  Thread::LockGuard lock(mutex_);
  return system_time_;
}

MonotonicTime SimulatedTimeSystemHelper::monotonicTime() {
  Thread::LockGuard lock(mutex_);
  return monotonic_time_;
}

void SimulatedTimeSystemHelper::sleep(const Duration& duration) {
  mutex_.lock();
  MonotonicTime monotonic_time =
      monotonic_time_ + std::chrono::duration_cast<MonotonicTime::duration>(duration);
  setMonotonicTimeAndUnlock(monotonic_time);
}

Thread::CondVar::WaitStatus SimulatedTimeSystemHelper::waitFor(Thread::MutexBasicLockable& mutex,
                                                               Thread::CondVar& condvar,
                                                               const Duration& duration) noexcept {
  const Duration real_time_poll_delay(
      std::min(std::chrono::duration_cast<Duration>(std::chrono::milliseconds(50)), duration));
  const MonotonicTime end_time = monotonicTime() + duration;

  while (true) {
    // First check to see if the condition is already satisfied without advancing sim time.
    if (condvar.waitFor(mutex, real_time_poll_delay) == Thread::CondVar::WaitStatus::NoTimeout) {
      return Thread::CondVar::WaitStatus::NoTimeout;
    }

    // Wait for the libevent poll in another thread to catch up prior to advancing time.
    if (hasPending()) {
      continue;
    }

    mutex_.lock();
    if (monotonic_time_ < end_time) {
      if (alarms_.empty()) {
        // If no alarms are pending, sleep till the end time.
        setMonotonicTimeAndUnlock(end_time);
      } else {
        // If there's another alarm pending, sleep forward to it.
        MonotonicTime next_wakeup = (*alarms_.begin())->time();
        setMonotonicTimeAndUnlock(std::min(next_wakeup, end_time));
      }
    } else {
      // If we reached our end_time, break the loop and return timeout.
      mutex_.unlock();
      break;
    }
  }
  return Thread::CondVar::WaitStatus::Timeout;
}

int64_t SimulatedTimeSystemHelper::nextIndex() {
  Thread::LockGuard lock(mutex_);
  return index_++;
}

void SimulatedTimeSystemHelper::addAlarmLockHeld(Alarm* alarm,
                                                 const std::chrono::milliseconds& duration) {
  alarm->setTimeLockHeld(monotonic_time_ + duration);
  alarms_.insert(alarm);
}

void SimulatedTimeSystemHelper::removeAlarmLockHeld(Alarm* alarm) { alarms_.erase(alarm); }

SchedulerPtr SimulatedTimeSystemHelper::createScheduler(Scheduler& base_scheduler) {
  return std::make_unique<SimulatedScheduler>(*this, base_scheduler);
}

void SimulatedTimeSystemHelper::setMonotonicTimeAndUnlock(const MonotonicTime& monotonic_time) {
  // We don't have a convenient LockGuard construct that allows temporarily
  // dropping the lock to run a callback. The main issue here is that we must
  // be careful not to be holding mutex_ when an exception can be thrown.
  // That can only happen here in alarm->activate(), which is run with the mutex
  // released.
  if (monotonic_time >= monotonic_time_) {
    // Alarms is a std::set ordered by wakeup time, so pulling off begin() each
    // iteration gives you wakeup order. Also note that alarms may be added
    // or removed during the call to activate() so it would not be correct to
    // range-iterate over the set.
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
      alarm->activateLockHeld();
    }
    system_time_ +=
        std::chrono::duration_cast<SystemTime::duration>(monotonic_time - monotonic_time_);
    monotonic_time_ = monotonic_time;
  }
  mutex_.unlock();
}

void SimulatedTimeSystemHelper::setSystemTime(const SystemTime& system_time) {
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
