#include "common/common/thread.h"
#include "common/event/libevent.h"

#include "test/test_common/simulated_time_system.h"

#include "event2/event.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace Test {

class SimulatedTimeSystemTest : public testing::Test {
protected:
  SimulatedTimeSystemTest()
      : event_system_(event_base_new()), scheduler_(sim_.createScheduler(event_system_)),
        start_monotonic_time_(sim_.monotonicTime()), start_system_time_(sim_.systemTime()) {}

  void addTask(int64_t delay_ms, char marker) {
    std::chrono::milliseconds delay(delay_ms);
    TimerPtr timer = scheduler_->createTimer([this, marker, delay]() {
      output_.append(1, marker);
      EXPECT_GE(sim_.monotonicTime(), start_monotonic_time_ + delay);
    });
    timer->enableTimer(delay);
    timers_.push_back(std::move(timer));
  }

  void sleepMsAndLoop(int64_t delay_ms) {
    sim_.sleep(std::chrono::milliseconds(delay_ms));
    event_base_loop(event_system_.get(), EVLOOP_NONBLOCK);
  }

  void advanceSystemMsAndLoop(int64_t delay_ms) {
    sim_.setSystemTime(sim_.systemTime() + std::chrono::milliseconds(delay_ms));
    event_base_loop(event_system_.get(), EVLOOP_NONBLOCK);
  }

  SimulatedTimeSystem sim_;
  Libevent::BasePtr event_system_;
  SchedulerPtr scheduler_;
  std::string output_;
  std::vector<TimerPtr> timers_;
  MonotonicTime start_monotonic_time_;
  SystemTime start_system_time_;
};

TEST_F(SimulatedTimeSystemTest, Sleep) {
  EXPECT_EQ(start_monotonic_time_, sim_.monotonicTime());
  EXPECT_EQ(start_system_time_, sim_.systemTime());
  sleepMsAndLoop(5);
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), sim_.monotonicTime());
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), sim_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, WaitFor) {
  EXPECT_EQ(start_monotonic_time_, sim_.monotonicTime());
  EXPECT_EQ(start_system_time_, sim_.systemTime());

  // Run an event loop in the background to activate timers.
  std::atomic<bool> done(false);
  auto thread = std::make_unique<Thread::Thread>([this, &done]() {
    while (!done) {
      event_base_loop(event_system_.get(), 0);
    }
  });
  Thread::CondVar condvar;
  Thread::MutexBasicLockable mutex;
  TimerPtr timer = scheduler_->createTimer([&condvar, &mutex, &done]() {
    Thread::LockGuard lock(mutex);
    done = true;
    condvar.notifyOne();
  });
  timer->enableTimer(std::chrono::seconds(60));

  // Wait 50 simulated seconds of simulated time, which won't be enough to
  // activate the alarm. We'll get a fast automatic timeout in waitFor because
  // there are no pending timers.
  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::Timeout,
              sim_.waitFor(mutex, condvar, std::chrono::seconds(50)));
  }
  EXPECT_FALSE(done);

  // Waiting another 10 simulated seconds will activate the alarm, and the
  // event-loop thread will call the corresponding callback quickly.
  {
    Thread::LockGuard lock(mutex);
    EXPECT_EQ(Thread::CondVar::WaitStatus::NoTimeout,
              sim_.waitFor(mutex, condvar, std::chrono::seconds(10)));
  }
  EXPECT_TRUE(done);

  thread->join();
}

TEST_F(SimulatedTimeSystemTest, Monotonic) {
  // Setting time forward works.
  sim_.setMonotonicTime(start_monotonic_time_ + std::chrono::milliseconds(5));
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), sim_.monotonicTime());

  // But going backward does not.
  sim_.setMonotonicTime(start_monotonic_time_ + std::chrono::milliseconds(3));
  EXPECT_EQ(start_monotonic_time_ + std::chrono::milliseconds(5), sim_.monotonicTime());
}

TEST_F(SimulatedTimeSystemTest, System) {
  // Setting time forward works.
  sim_.setSystemTime(start_system_time_ + std::chrono::milliseconds(5));
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(5), sim_.systemTime());

  // And going backward works too.
  sim_.setSystemTime(start_system_time_ + std::chrono::milliseconds(3));
  EXPECT_EQ(start_system_time_ + std::chrono::milliseconds(3), sim_.systemTime());
}

TEST_F(SimulatedTimeSystemTest, Ordering) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  EXPECT_EQ("", output_);
  sleepMsAndLoop(5);
  EXPECT_EQ("35", output_);
  sleepMsAndLoop(1);
  EXPECT_EQ("356", output_);
}

TEST_F(SimulatedTimeSystemTest, SystemTimeOrdering) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  EXPECT_EQ("", output_);
  advanceSystemMsAndLoop(5);
  EXPECT_EQ("35", output_);
  advanceSystemMsAndLoop(1);
  EXPECT_EQ("356", output_);
  sim_.setSystemTime(start_system_time_ + std::chrono::milliseconds(1));
  sim_.setSystemTime(start_system_time_ + std::chrono::milliseconds(100));
  EXPECT_EQ("356", output_); // callbacks don't get replayed.
}

TEST_F(SimulatedTimeSystemTest, DisableTimer) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  timers_[0]->disableTimer();
  EXPECT_EQ("", output_);
  sleepMsAndLoop(5);
  EXPECT_EQ("3", output_);
  sleepMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

TEST_F(SimulatedTimeSystemTest, IgnoreRedundantDisable) {
  addTask(5, '5');
  timers_[0]->disableTimer();
  timers_[0]->disableTimer();
  sleepMsAndLoop(5);
  EXPECT_EQ("", output_);
}

TEST_F(SimulatedTimeSystemTest, OverrideEnable) {
  addTask(5, '5');
  timers_[0]->enableTimer(std::chrono::milliseconds(6));
  sleepMsAndLoop(5);
  EXPECT_EQ("", output_);  // Timer didn't wake up because we overrode to 6ms.
  sleepMsAndLoop(1);
  EXPECT_EQ("5", output_);
}

TEST_F(SimulatedTimeSystemTest, DeleteTime) {
  addTask(5, '5');
  addTask(3, '3');
  addTask(6, '6');
  timers_[0].reset();
  EXPECT_EQ("", output_);
  sleepMsAndLoop(5);
  EXPECT_EQ("3", output_);
  sleepMsAndLoop(1);
  EXPECT_EQ("36", output_);
}

} // namespace Test
} // namespace Event
} // namespace Envoy
