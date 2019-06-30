#include "common/stats/heap_stat_data.h"

#include <cstdint>

#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/common/utility.h"
#include "common/stats/metric_impl.h"
#include "common/stats/stat_merger.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

HeapStatDataAllocator::~HeapStatDataAllocator() {
  ASSERT(counters_.empty());
  ASSERT(gauges_.empty());
}

/*
HeapStatData* HeapStatData::alloc(StatName stat_name, SymbolTable& symbol_table) {
  symbol_table.incRefCount(stat_name);
  return new (stat_name.size()) HeapStatData(stat_name);
}

void HeapStatData::free(SymbolTable& symbol_table) {
  ASSERT(ref_count_ == 0);
  symbol_table.free(statName());
  delete this;
}
*/

void HeapStatDataAllocator::removeCounterFromSet(Counter* counter) {
  Thread::LockGuard lock(mutex_);
  const size_t count = counters_.erase(counter->statName());
  ASSERT(count == 1);
}

void HeapStatDataAllocator::removeGaugeFromSet(Gauge* gauge) {
  Thread::LockGuard lock(mutex_);
  const size_t count = gauges_.erase(gauge->statName());
  ASSERT(count == 1);
}

#ifndef ENVOY_CONFIG_COVERAGE
void HeapStatDataAllocator::debugPrint() {
  Thread::LockGuard lock(mutex_);
  for (Counter* counter : counters_) {
    ENVOY_LOG_MISC(info, "counter: {}", symbolTable().toString(counter->statName()));
  }
  for (Gauge* gauge : gauges_) {
    ENVOY_LOG_MISC(info, "gauge: {}", symbolTable().toString(gauge->statName()));
  }
}
#endif

template <class BaseClass> class StatsSharedImpl : public MetricImpl<BaseClass> {
public:
  explicit StatsSharedImpl(StatName name, HeapStatDataAllocator& alloc,
                           absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : MetricImpl<BaseClass>(name, tag_extracted_name, tags, alloc.symbolTable()), alloc_(alloc) {}

  ~StatsSharedImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    this->clear(symbolTable());
  }

  // Metric
  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }

  // Counter/Gauge
  bool used() const override { return data_.flags_ & Metric::Flags::Used; }

  // RefcountInterface
  void incRefCount() override { ++data_.ref_count_; }
  bool decRefCount() override {
    ASSERT(data_.ref_count_ >= 1);
    return --data_.ref_count_ == 0;
  }
  uint32_t use_count() const override { return data_.ref_count_; }

protected:
  HeapStatData data_;
  HeapStatDataAllocator& alloc_;
};

class CounterImpl : public StatsSharedImpl<Counter> {
public:
  CounterImpl(StatName name, HeapStatDataAllocator& alloc, absl::string_view tag_extracted_name,
              const std::vector<Tag>& tags)
      : StatsSharedImpl(name, alloc, tag_extracted_name, tags) {}
  ~CounterImpl() override { alloc_.removeCounterFromSet(this); }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= Flags::Used;
  }
  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & Flags::Used; }
  uint64_t value() const override { return data_.value_; }
};

class GaugeImpl : public StatsSharedImpl<Gauge> {
public:
  GaugeImpl(StatName name, HeapStatDataAllocator& alloc, absl::string_view tag_extracted_name,
            const std::vector<Tag>& tags, ImportMode import_mode)
      : StatsSharedImpl(name, alloc, tag_extracted_name, tags) {
    switch (import_mode) {
    case ImportMode::Accumulate:
      data_.flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      data_.flags_ |= Flags::NeverImport;
      break;
    case ImportMode::Uninitialized:
      // Note that we don't clear any flag bits for import_mode==Uninitialized,
      // as we may have an established import_mode when this stat was created in
      // an alternate scope. See
      // https://github.com/envoyproxy/envoy/issues/7227.
      break;
    }
  }
  ~GaugeImpl() override { alloc_.removeGaugeFromSet(this); }

  // Stats::Gauge
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= Flags::Used;
  }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= Flags::Used;
  }
  void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used() || amount == 0);
    data_.value_ -= amount;
  }
  uint64_t value() const override { return data_.value_; }

  ImportMode importMode() const override {
    if (data_.flags_ & Flags::NeverImport) {
      return ImportMode::NeverImport;
    } else if (data_.flags_ & Flags::LogicAccumulate) {
      return ImportMode::Accumulate;
    }
    return ImportMode::Uninitialized;
  }

  void mergeImportMode(ImportMode import_mode) override {
    ImportMode current = importMode();
    if (current == import_mode) {
      return;
    }

    switch (import_mode) {
    case ImportMode::Uninitialized:
      // mergeImportNode(ImportMode::Uninitialized) is called when merging an
      // existing stat with importMode() == Accumulate or NeverImport.
      break;
    case ImportMode::Accumulate:
      ASSERT(current == ImportMode::Uninitialized);
      data_.flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      ASSERT(current == ImportMode::Uninitialized);
      // A previous revision of Envoy may have transferred a gauge that it
      // thought was Accumulate. But the new version thinks it's NeverImport, so
      // we clear the accumulated value.
      data_.value_ = 0;
      data_.flags_ &= ~Flags::Used;
      data_.flags_ |= Flags::NeverImport;
      break;
    }
  }
};

CounterSharedPtr HeapStatDataAllocator::makeCounter(StatName name,
                                                    absl::string_view tag_extracted_name,
                                                    const std::vector<Tag>& tags) {
  Thread::LockGuard lock(mutex_);
  auto iter = counters_.find(name);
  if (iter != counters_.end()) {
    return CounterSharedPtr(*iter);
  }
  auto counter = CounterSharedPtr(new CounterImpl(name, *this, tag_extracted_name, tags));
  counters_.insert(counter.get());
  return counter;
}

GaugeSharedPtr HeapStatDataAllocator::makeGauge(StatName name, absl::string_view tag_extracted_name,
                                                const std::vector<Tag>& tags,
                                                Gauge::ImportMode import_mode) {
  Thread::LockGuard lock(mutex_);
  auto iter = gauges_.find(name);
  if (iter != gauges_.end()) {
    return GaugeSharedPtr(*iter);
  }
  auto gauge = GaugeSharedPtr(new GaugeImpl(name, *this, tag_extracted_name, tags, import_mode));
  gauges_.insert(gauge.get());
  return gauge;
}

} // namespace Stats
} // namespace Envoy
