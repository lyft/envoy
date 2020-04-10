#include <string>

#include "envoy/stats/stats_macros.h"

#include "common/stats/isolated_store_impl.h"
#include "common/stats/null_counter.h"
#include "common/stats/null_gauge.h"
#include "common/stats/symbol_table_creator.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatsUtilityTest : public testing::Test {
protected:
  StatsUtilityTest()
      : symbol_table_(SymbolTableCreator::makeSymbolTable()),
        store_(std::make_unique<IsolatedStoreImpl>(*symbol_table_)), pool_(*symbol_table_),
        tags_(
            {{pool_.add("tag1"), pool_.add("value1")}, {pool_.add("tag2"), pool_.add("value2")}}) {}

  ~StatsUtilityTest() override {
    pool_.clear();
    store_.reset();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  SymbolTablePtr symbol_table_;
  std::unique_ptr<IsolatedStoreImpl> store_;
  StatNamePool pool_;
  StatNameTagVector tags_;
};

TEST_F(StatsUtilityTest, Counters) {
  ScopePtr scope = store_->createScope("scope.");
  Counter& c1 = Utility::counterFromElements(*scope, {"a", "b"});
  EXPECT_EQ("scope.a.b", c1.name());
  StatName token = pool_.add("token");
  Counter& c2 = Utility::counterFromElements(*scope, {"a", token, "b"});
  EXPECT_EQ("scope.a.token.b", c2.name());
  StatName suffix = pool_.add("suffix");
  Counter& c3 = Utility::counterFromElements(*scope, {token, suffix});
  EXPECT_EQ("scope.token.suffix", c3.name());

  Counter& ctags = Utility::counterFromElements(*scope, {"x", token, "y"}, tags_);
  EXPECT_EQ("scope.x.token.y.tag1.value1.tag2.value2", ctags.name());
}

TEST_F(StatsUtilityTest, Gauges) {
  ScopePtr scope = store_->createScope("scope.");
  Gauge& g1 = Utility::gaugeFromElements(*scope, {"a", "b"}, Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.a.b", g1.name());
  EXPECT_EQ(Gauge::ImportMode::NeverImport, g1.importMode());
  StatName token = pool_.add("token");
  Gauge& g2 = Utility::gaugeFromElements(*scope, {"a", token, "b"}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.a.token.b", g2.name());
  EXPECT_EQ(Gauge::ImportMode::Accumulate, g2.importMode());
  StatName suffix = pool_.add("suffix");
  Gauge& g3 = Utility::gaugeFromElements(*scope, {token, suffix}, Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.token.suffix", g3.name());
}

TEST_F(StatsUtilityTest, Histograms) {
  ScopePtr scope = store_->createScope("scope.");
  Histogram& h1 = Utility::histogramFromElements(*scope, {"a", "b"}, Histogram::Unit::Milliseconds);
  EXPECT_EQ("scope.a.b", h1.name());
  EXPECT_EQ(Histogram::Unit::Milliseconds, h1.unit());
  StatName token = pool_.add("token");
  Histogram& h2 =
      Utility::histogramFromElements(*scope, {"a", token, "b"}, Histogram::Unit::Microseconds);
  EXPECT_EQ("scope.a.token.b", h2.name());
  EXPECT_EQ(Histogram::Unit::Microseconds, h2.unit());
  StatName suffix = pool_.add("suffix");
  Histogram& h3 = Utility::histogramFromElements(*scope, {token, suffix}, Histogram::Unit::Bytes);
  EXPECT_EQ("scope.token.suffix", h3.name());
  EXPECT_EQ(Histogram::Unit::Bytes, h3.unit());
}

} // namespace
} // namespace Stats
} // namespace Envoy
