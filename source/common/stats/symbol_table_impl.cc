#include "common/stats/symbol_table_impl.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

SymbolEncoding::~SymbolEncoding() {
  ASSERT(vec_.empty());
}

void SymbolEncoding::addSymbol(Symbol symbol) {
  // UTF-8-like encoding where a value 127 or less gets written as a single
  // byte. For higher values we write the low-order 7 bits with a 1 in
  // the high-order bit. Then we right-shift 7 bits and keep adding more bytes
  // until we have consumed all the non-zero bits in symbol.
  //
  // When decoding, we stop consuming uint8_t when we see a uint8_t with
  // high-order bit 0.
  do {
    if (symbol < (1 << 7)) {
      vec_.push_back(symbol);
    } else {
      vec_.push_back((symbol & 0x7f) | 0x80);
    }
    symbol >>= 7;
  } while (symbol != 0);
}

SymbolVec SymbolEncoding::decodeSymbols(const SymbolStorage array, size_t size) {
  SymbolVec symbol_vec;
  Symbol symbol = 0;
  uint32_t shift = 0;
  while (true) {
    uint32_t uc = static_cast<uint32_t>(*array);
    ASSERT(size > 0);
    --size;
    ++array;
    symbol |= (uc & 0x7f) << shift;
    if ((uc & 0x80) == 0) {
      symbol_vec.push_back(symbol);
      if (size == 0) {
        return symbol_vec;
      }
      shift = 0;
      symbol = 0;
    } else {
      shift += 7;
    }
  }
}

void SymbolEncoding::moveToStorage(SymbolStorage symbol_array) {
  size_t sz = size();
  ASSERT(sz < 65536);
  symbol_array[0] = sz & 0xff;
  symbol_array[1] = sz >> 8;
  memcpy(symbol_array + 2, vec_.data(), sz * sizeof(uint8_t));
  vec_.clear();
}

SymbolTable::SymbolTable()
    // Have to be explicitly initialized, if we want to use the GUARDED_BY macro.
    : next_symbol_ (0),
      monotonic_counter_(0) {}

SymbolTable::~SymbolTable() {
  // To avoid leaks into the symbol table, we expect all StatNames to be freed.
  // Note: this could potentially be short-circuited if we decide a fast exit
  // is needed in production. But it would be good to ensure clean up during
  // tests.
  ASSERT(numSymbols() == 0);
}

// TODO(ambuc): There is a possible performance optimization here for avoiding
// the encoding of IPs / numbers if they appear in stat names. We don't want to
// waste time symbolizing an integer as an integer, if we can help it.
SymbolEncoding SymbolTable::encode(const absl::string_view name) {
  // We want to hold the lock for the minimum amount of time, so we do the
  // string-splitting and prepare a temp vector of Symbol first.
  std::vector<absl::string_view> tokens = absl::StrSplit(name, '.');
  std::vector<Symbol> symbols;
  symbols.reserve(tokens.size());

  // Now take the lock and populte the Symbol objects, which involves bumping
  // ref-counts in this.
  {
    Thread::LockGuard lock(lock_);
    for (absl::string_view token : tokens) {
      symbols.push_back(toSymbol(token));
    }
  }

  // Now efficiently encode the array of 32-bit symbols into a uint8_t array.
  SymbolEncoding encoding;
  for (Symbol symbol : symbols) {
    encoding.addSymbol(symbol);
  }
  return encoding;
}

std::string SymbolTable::decode(const SymbolStorage symbol_array, size_t size) const {
  // Before taking the lock, decode the array of symbols from the SymbolStorage.
  SymbolVec symbols = SymbolEncoding::decodeSymbols(symbol_array, size);

  std::vector<absl::string_view> name_tokens;
  name_tokens.reserve(symbols.size());
  {
    Thread::LockGuard lock(lock_);
    for (Symbol symbol : symbols) {
      name_tokens.push_back(fromSymbol(symbol));
    }
  }
  return absl::StrJoin(name_tokens, ".");
}

void SymbolTable::free(const SymbolStorage symbol_array, size_t size) {
  // Before taking the lock, decode the array of symbols from the SymbolStorage.
  SymbolVec symbols = SymbolEncoding::decodeSymbols(symbol_array, size);

  Thread::LockGuard lock(lock_);
  for (Symbol symbol : symbols) {
    auto decode_search = decode_map_.find(symbol);
    ASSERT(decode_search != decode_map_.end());

    auto encode_search = encode_map_.find(decode_search->second);
    ASSERT(encode_search != encode_map_.end());

    encode_search->second.ref_count_--;
    // If that was the last remaining client usage of the symbol, erase the the current
    // mappings and add the now-unused symbol to the reuse pool.
    if (encode_search->second.ref_count_ == 0) {
      decode_map_.erase(decode_search);
      encode_map_.erase(encode_search);
      pool_.push(symbol);
    }
  }
}

Symbol SymbolTable::toSymbol(absl::string_view sv) EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  Symbol result;
  auto encode_find = encode_map_.find(sv);
  // If the string segment doesn't already exist,
  if (encode_find == encode_map_.end()) {
    // We create the actual string, place it in the decode_map_, and then insert a string_view
    // pointing to it in the encode_map_. This allows us to only store the string once.
    std::string str = std::string(sv);

    auto decode_insert = decode_map_.insert({next_symbol_, std::move(str)});
    ASSERT(decode_insert.second);

    auto encode_insert = encode_map_.insert(
        {decode_insert.first->second, {.symbol_ = next_symbol_, .ref_count_ = 1}});
    ASSERT(encode_insert.second);

    result = next_symbol_;
    newSymbol();
  } else {
    // If the insertion didn't take place, return the actual value at that location and up the
    // refcount at that location
    result = encode_find->second.symbol_;
    ++(encode_find->second.ref_count_);
  }
  return result;
}

absl::string_view SymbolTable::fromSymbol(const Symbol symbol) const
    EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  auto search = decode_map_.find(symbol);
  ASSERT(search != decode_map_.end());
  return search->second;
}

void SymbolTable::newSymbol() EXCLUSIVE_LOCKS_REQUIRED(lock_) {
  if (pool_.empty()) {
    next_symbol_ = ++monotonic_counter_;
  } else {
    next_symbol_ = pool_.top();
    pool_.pop();
  }
  // This should catch integer overflow for the new symbol.
  ASSERT(monotonic_counter_ != 0);
}

StatNameStorage::StatNameStorage(absl::string_view name, SymbolTable& table) {
  SymbolEncoding encoding = table.encode(name);
  bytes_.reset(new uint8_t[encoding.bytesRequired()]);
  encoding.moveToStorage(bytes_.get());
}

StatNameStorage::~StatNameStorage() {
  // StatNameStorage is not fully RAII: you must free it with
  ASSERT(bytes_.get() == nullptr);
}

void StatNameStorage::free(SymbolTable& table) {
  statName().free(table);
  bytes_.reset();
}

} // namespace Stats
} // namespace Envoy
