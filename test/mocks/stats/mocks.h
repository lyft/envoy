#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/stats/stats_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

class MockMetric : public Metric {
public:
  MockMetric();
  ~MockMetric();

  MOCK_CONST_METHOD0(name, std::string());
  MOCK_CONST_METHOD0(tagExtractedName, const std::string&());
  MOCK_CONST_METHOD0(tags, const std::vector<Tag>&());

  std::string name_;
};

class MockCounter : public Counter {
public:
  MockCounter();
  ~MockCounter();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(inc, void());
  MOCK_METHOD0(latch, uint64_t());
  MOCK_CONST_METHOD0(name, std::string());
  MOCK_CONST_METHOD0(tagExtractedName, const std::string&());
  MOCK_CONST_METHOD0(tags, const std::vector<Tag>&());
  MOCK_METHOD0(reset, void());
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());
};

class MockGauge : public Gauge {
public:
  MockGauge();
  ~MockGauge();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(dec, void());
  MOCK_METHOD0(inc, void());
  MOCK_CONST_METHOD0(name, std::string());
  MOCK_CONST_METHOD0(tagExtractedName, const std::string&());
  MOCK_CONST_METHOD0(tags, const std::vector<Tag>&());
  MOCK_METHOD1(set, void(uint64_t value));
  MOCK_METHOD1(sub, void(uint64_t amount));
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());
};

class MockTimespan : public Timespan {
public:
  MockTimespan();
  ~MockTimespan();

  MOCK_METHOD0(complete, void());
  MOCK_METHOD1(complete, void(const std::string& dynamic_name));
};

class MockTimer : public Timer {
public:
  MockTimer();
  ~MockTimer();

  // Note: cannot be mocked because it is accessed as a Property in a gmock EXPECT_CALL. This
  // creates a deadlock in gmock and is an unintended use of mock functions.
  std::string name() const override { return name_; };
  MOCK_METHOD0(allocateSpan, TimespanPtr());
  MOCK_METHOD1(recordDuration, void(std::chrono::milliseconds ms));
  MOCK_CONST_METHOD0(tagExtractedName, const std::string&());
  MOCK_CONST_METHOD0(tags, const std::vector<Tag>&());

  std::string name_;
  Store* store_;
};

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink();

  MOCK_METHOD0(beginFlush, void());
  MOCK_METHOD2(flushCounter, void(const Metric& counter, uint64_t delta));
  MOCK_METHOD2(flushGauge, void(const Metric& gauge, uint64_t value));
  MOCK_METHOD0(endFlush, void());
  MOCK_METHOD2(onHistogramComplete, void(const Metric& histogram, uint64_t value));
  MOCK_METHOD2(onTimespanComplete, void(const Metric& timer, std::chrono::milliseconds ms));
};

class MockStore : public Store {
public:
  MockStore();
  ~MockStore();

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD2(deliverHistogramToSinks, void(const Metric& histogram, uint64_t value));
  MOCK_METHOD2(deliverTimingToSinks, void(const Metric& timer, std::chrono::milliseconds ms));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_CONST_METHOD0(counters, std::list<CounterSharedPtr>());
  MOCK_METHOD1(createScope_, Scope*(const std::string& name));
  MOCK_METHOD1(gauge, Gauge&(const std::string&));
  MOCK_CONST_METHOD0(gauges, std::list<GaugeSharedPtr>());
  MOCK_METHOD1(timer, Timer&(const std::string& name));
  MOCK_METHOD1(histogram, Histogram&(const std::string& name));

  testing::NiceMock<MockCounter> counter_;
  std::vector<std::unique_ptr<MockTimer>> timers_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverTimingToSinks for better testing.
 */
class MockIsolatedStatsStore : public IsolatedStoreImpl {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore();

  MOCK_METHOD2(deliverTimingToSinks, void(const Metric& timer, std::chrono::milliseconds));
};

} // namespace Stats
} // namespace Envoy
