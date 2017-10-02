#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

#include "envoy/common/time.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/singleton.h"
#include "common/protobuf/protobuf.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Stats {

class TagExtractorImpl : public TagExtractor {
public:
  /**
   * Creates a tag extractor from the regex provided or looks up a default regex.
   * @param name name for tag extractor. Used to look up a default tag extractor if regex is empty.
   * @param regex optional regex expression. Can be specified as an empty string to trigger a
   * default regex lookup.
   */
  static TagExtractorPtr createTagExtractor(const std::string& name, const std::string& regex);

  TagExtractorImpl(const std::string& name, const std::string& regex);
  std::string name() const override { return name_; }
  std::string updateTags(const std::string& tag_extracted_name,
                         std::vector<Tag>& tags) const override;

private:
  std::string name_;
  std::regex regex_;
};

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 */
struct RawStatData {
  struct Flags {
    static const uint8_t Used = 0x1;
  };

  static const size_t MAX_NAME_SIZE = 127;

  RawStatData() { memset(name_, 0, sizeof(name_)); }
  void initialize(const std::string& name);
  bool initialized() { return name_[0] != '\0'; }
  bool matches(const std::string& name);

  std::atomic<uint64_t> value_;
  std::atomic<uint64_t> pending_increment_;
  std::atomic<uint16_t> flags_;
  std::atomic<uint16_t> ref_count_;
  std::atomic<uint32_t> unused_;
  char name_[MAX_NAME_SIZE + 1];
};

/**
 * Abstract interface for allocating a RawStatData.
 */
class RawStatDataAllocator {
public:
  virtual ~RawStatDataAllocator() {}

  /**
   * @return RawStatData* a raw stat data block for a given stat name or nullptr if there is no more
   *         memory available for stats. The allocator may return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  virtual RawStatData* alloc(const std::string& name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   */
  virtual void free(RawStatData& data) PURE;
};

/**
 * Counter implementation that wraps a RawStatData.
 */
class CounterImpl : public Counter {
public:
  CounterImpl(RawStatData& data, RawStatDataAllocator& alloc, std::string&& tag_extracted_name,
              std::vector<Tag>&& tags)
      : data_(data), alloc_(alloc), tag_extracted_name_(std::move(tag_extracted_name)),
        tags_(std::move(tags)) {}
  ~CounterImpl() { alloc_.free(data_); }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }
  uint64_t value() const override { return data_.value_; }

  // Stats::Metric
  std::string name() const override { return data_.name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Gauge implementation that wraps a RawStatData.
 */
class GaugeImpl : public Gauge {
public:
  GaugeImpl(RawStatData& data, RawStatDataAllocator& alloc, std::string&& tag_extracted_name,
            std::vector<Tag>&& tags)
      : data_(data), alloc_(alloc), tag_extracted_name_(std::move(tag_extracted_name)),
        tags_(std::move(tags)) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  virtual uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }

  // Stats::Metric
  virtual std::string name() const override { return data_.name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Timer implementation for the heap.
 */
class TimerImpl : public Timer {
public:
  TimerImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
            std::vector<Tag>&& tags)
      : name_(name), parent_(parent), tag_extracted_name_(std::move(tag_extracted_name)),
        tags_(std::move(tags)) {}

  // Stats::Timer
  TimespanPtr allocateSpan() override { return TimespanPtr{new TimespanImpl(*this)}; }
  void recordDuration(std::chrono::milliseconds ma) override;

  // Stats::Metric
  std::string name() const override { return name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

private:
  /**
   * Timespan implementation for the heap.
   */
  class TimespanImpl : public Timespan {
  public:
    TimespanImpl(TimerImpl& parent) : parent_(parent), start_(std::chrono::steady_clock::now()) {}

    // Stats::Timespan
    void complete() override { complete(parent_.name_); }
    void complete(const std::string& dynamic_name) override;

  private:
    TimerImpl& parent_;
    MonotonicTime start_;
  };

  std::string name_;
  Store& parent_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram {
public:
  HistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                std::vector<Tag>&& tags)
      : name_(name), parent_(parent), tag_extracted_name_(std::move(tag_extracted_name)),
        tags_(std::move(tags)) {}

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  // Stats::Metric
  std::string name() const override { return name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

  std::string name_;
  Store& parent_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Implementation of RawStatDataAllocator that just allocates a new structure in memory and returns
 * it.
 */
class HeapRawStatDataAllocator : public RawStatDataAllocator {
public:
  // RawStatDataAllocator
  RawStatData* alloc(const std::string& name) override;
  void free(RawStatData& data) override;
};

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base, class Impl> class IsolatedStatsCache {
public:
  typedef std::function<Impl*(const std::string& name)> Allocator;

  IsolatedStatsCache(Allocator alloc) : alloc_(alloc) {}

  Base& get(const std::string& name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    Impl* new_stat = alloc_(name);
    stats_.emplace(name, std::shared_ptr<Impl>{new_stat});
    return *new_stat;
  }

  std::list<std::shared_ptr<Base>> toList() const {
    std::list<std::shared_ptr<Base>> list;
    for (auto& stat : stats_) {
      list.push_back(stat.second);
    }

    return list;
  }

private:
  std::unordered_map<std::string, std::shared_ptr<Impl>> stats_;
  Allocator alloc_;
};

/**
 * Store implementation that is isolated from other stores.
 */
class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl()
      : counters_([this](const std::string& name) -> CounterImpl* {
          return new CounterImpl(*alloc_.alloc(name), alloc_, std::string(name),
                                 std::vector<Tag>());
        }),
        gauges_([this](const std::string& name) -> GaugeImpl* {
          return new GaugeImpl(*alloc_.alloc(name), alloc_, std::string(name), std::vector<Tag>());
        }),
        timers_([this](const std::string& name) -> TimerImpl* {
          return new TimerImpl(name, *this, std::string(name), std::vector<Tag>());
        }),
        histograms_([this](const std::string& name) -> HistogramImpl* {
          return new HistogramImpl(name, *this, std::string(name), std::vector<Tag>());
        }) {}

  // Stats::Scope
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  ScopePtr createScope(const std::string& name) override {
    return ScopePtr{new ScopeImpl(*this, name)};
  }
  void deliverHistogramToSinks(const Metric&, uint64_t) override {}
  void deliverTimingToSinks(const Metric&, std::chrono::milliseconds) override {}
  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  Timer& timer(const std::string& name) override { return timers_.get(name); }
  Histogram& histogram(const std::string& name) override { return histograms_.get(name); }

  // Stats::Store
  std::list<CounterSharedPtr> counters() const override { return counters_.toList(); }
  std::list<GaugeSharedPtr> gauges() const override { return gauges_.toList(); }

private:
  struct ScopeImpl : public Scope {
    ScopeImpl(IsolatedStoreImpl& parent, const std::string& prefix)
        : parent_(parent), prefix_(prefix) {}

    // Stats::Scope
    ScopePtr createScope(const std::string& name) override {
      return ScopePtr{new ScopeImpl(parent_, prefix_ + name)};
    }
    void deliverHistogramToSinks(const Metric&, uint64_t) override {}
    void deliverTimingToSinks(const Metric&, std::chrono::milliseconds) override {}
    Counter& counter(const std::string& name) override { return parent_.counter(prefix_ + name); }
    Gauge& gauge(const std::string& name) override { return parent_.gauge(prefix_ + name); }
    Timer& timer(const std::string& name) override { return parent_.timer(prefix_ + name); }
    Histogram& histogram(const std::string& name) override {
      return parent_.histogram(prefix_ + name);
    }

    IsolatedStoreImpl& parent_;
    const std::string prefix_;
  };

  HeapRawStatDataAllocator alloc_;
  IsolatedStatsCache<Counter, CounterImpl> counters_;
  IsolatedStatsCache<Gauge, GaugeImpl> gauges_;
  IsolatedStatsCache<Timer, TimerImpl> timers_;
  IsolatedStatsCache<Histogram, HistogramImpl> histograms_;
};

} // namespace Stats
} // namespace Envoy
