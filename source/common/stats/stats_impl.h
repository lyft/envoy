#pragma once

#include <algorithm>
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
#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/non_copyable.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/stats_options_impl.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "circllhist.h"

namespace Envoy {
namespace Stats {

// Partially implements a StatDataAllocator, leaving alloc & free for subclasses.
// We templatize on StatData rather than defining a virtual base StatData class
// for performance reasons; stat increment is on the hot path.
//
// The two production derivations cover using a fixed block of shared-memory for
// hot restart stat continuity, and heap allocation for more efficient RAM usage
// for when hot-restart is not required.
//
// Also note that RawStatData needs to live in a shared memory block, and it's
// possible, but not obvious, that a vptr would be usable across processes. In
// any case, RawStatData is allocated from a shared-memory block rather than via
// new, so the usual C++ compiler assistance for setting up vptrs will not be
// available. This could be resolved with placed new, or another nesting level.
template <class StatData> class StatDataAllocatorImpl : public StatDataAllocator {
public:
  // StatDataAllocator
  CounterSharedPtr makeCounter(absl::string_view name, std::string&& tag_extracted_name,
                               std::vector<Tag>&& tags) override;
  GaugeSharedPtr makeGauge(absl::string_view name, std::string&& tag_extracted_name,
                           std::vector<Tag>&& tags) override;

  /**
   * @param name the full name of the stat.
   * @return StatData* a data block for a given stat name or nullptr if there is no more memory
   *         available for stats. The allocator should return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  virtual StatData* alloc(absl::string_view name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   * @param data the data returned by alloc().
   */
  virtual void free(StatData& data) PURE;
};

class RawStatDataAllocator : public StatDataAllocatorImpl<RawStatData> {
public:
  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return true; }
};

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
 */
struct HeapStatData {
  explicit HeapStatData(absl::string_view key);

  /**
   * @returns absl::string_view the name as a string_view.
   */
  absl::string_view key() const { return name_; }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  std::string name_;
};

/**
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 */
class MetricImpl : public virtual Metric {
public:
  MetricImpl(const std::string& name, std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : name_(name), tag_extracted_name_(std::move(tag_extracted_name)), tags_(std::move(tags)) {}

  const std::string& name() const override { return name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

protected:
  /**
   * Flags used by all stats types to figure out whether they have been used.
   */
  struct Flags {
    static const uint8_t Used = 0x1;
  };

private:
  const std::string name_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Implementation of HistogramStatistics for circllhist.
 */
class HistogramStatisticsImpl : public HistogramStatistics, NonCopyable {
public:
  HistogramStatisticsImpl() : computed_quantiles_(supportedQuantiles().size(), 0.0) {}
  /**
   * HistogramStatisticsImpl object is constructed using the passed in histogram.
   * @param histogram_ptr pointer to the histogram for which stats will be calculated. This pointer
   * will not be retained.
   */
  HistogramStatisticsImpl(const histogram_t* histogram_ptr);

  void refresh(const histogram_t* new_histogram_ptr);

  // HistogramStatistics
  std::string summary() const override;
  const std::vector<double>& supportedQuantiles() const override;
  const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }

private:
  std::vector<double> computed_quantiles_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram, public MetricImpl {
public:
  HistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                std::vector<Tag>&& tags)
      : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), parent_(parent) {}

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }

private:
  // This is used for delivering the histogram data to sinks.
  Store& parent_;
};

class SourceImpl : public Source {
public:
  SourceImpl(Store& store) : store_(store){};

  // Stats::Source
  std::vector<CounterSharedPtr>& cachedCounters() override;
  std::vector<GaugeSharedPtr>& cachedGauges() override;
  std::vector<ParentHistogramSharedPtr>& cachedHistograms() override;
  void clearCache() override;

private:
  Store& store_;
  absl::optional<std::vector<CounterSharedPtr>> counters_;
  absl::optional<std::vector<GaugeSharedPtr>> gauges_;
  absl::optional<std::vector<ParentHistogramSharedPtr>> histograms_;
};

/**
 * Implementation of StatDataAllocator using a pure heap-based strategy, so that
 * Envoy implementations that do not require hot-restart can use less memory.
 */
class HeapStatDataAllocator : public StatDataAllocatorImpl<HeapStatData> {
public:
  HeapStatDataAllocator() {}
  ~HeapStatDataAllocator() { ASSERT(stats_.empty()); }

  // StatDataAllocatorImpl
  HeapStatData* alloc(absl::string_view name) override;
  void free(HeapStatData& data) override;

  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return false; }

private:
  struct HeapStatHash_ {
    size_t operator()(const HeapStatData* a) const { return HashUtil::xxHash64(a->key()); }
  };
  struct HeapStatCompare_ {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const {
      return (a->key() == b->key());
    }
  };

  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/3927 and
  //  https://github.com/envoyproxy/envoy/issues/3585, which can help reorganize
  // the heap stats using a ref-counted symbol table to compress the stat strings.
  typedef std::unordered_set<HeapStatData*, HeapStatHash_, HeapStatCompare_> StatSet;

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher.
  StatSet stats_ GUARDED_BY(mutex_);
  // A mutex is needed here to protect the stats_ object from both alloc() and free() operations.
  // Although alloc() operations are called under existing locking, free() operations are made from
  // the destructors of the individual stat objects, which are not protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
