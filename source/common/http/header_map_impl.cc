#include "common/http/header_map_impl.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {

namespace {
constexpr size_t MinDynamicCapacity{32};
// This includes the NULL (StringUtil::itoa technically only needs 21).
constexpr size_t MaxIntegerLength{32};

void validateCapacity(uint64_t new_capacity) {
  // If the resizing will cause buffer overflow due to hitting uint32_t::max, an OOM is likely
  // imminent. Fast-fail rather than allow a buffer overflow attack (issue #1421)
  RELEASE_ASSERT(new_capacity <= std::numeric_limits<uint32_t>::max(),
                 "Trying to allocate overly large headers.");
  ASSERT(new_capacity >= MinDynamicCapacity);
}
} // namespace

// Initialize as a Type::Inline
HeaderString::HeaderString() : buffer_(absl::InlinedVector<char, 128>()), string_length_(0) {
  static_assert(sizeof(buffer_) >= MaxIntegerLength, "");
  static_assert(MinDynamicCapacity >= MaxIntegerLength, "");
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(const LowerCaseString& ref_value)
    : buffer_(absl::string_view(ref_value.get().c_str(), ref_value.get().size())),
      string_length_(ref_value.get().size()) {
  ASSERT(valid());
}

// Initialize as a Type::Reference
HeaderString::HeaderString(absl::string_view ref_value)
    : buffer_(ref_value), string_length_(ref_value.size()) {
  ASSERT(valid());
}

HeaderString::HeaderString(HeaderString&& move_value) noexcept
    : buffer_(move_value.buffer_), string_length_(move_value.string_length_) {
  move_value.clear();
  ASSERT(valid());
}

HeaderString::~HeaderString() {}

bool HeaderString::valid() const { return validHeaderString(getStringView()); }

#define BUFFER_STR_VIEW_GET absl::get<absl::string_view>(buffer_)
#define BUFFER_IN_VEC_GET absl::get<absl::InlinedVector<char, 128>>(buffer_)

void HeaderString::append(const char* data, uint32_t size) {
  uint64_t new_capacity = static_cast<uint64_t>(size) + string_length_;
  if (new_capacity < MinDynamicCapacity) {
    new_capacity = MinDynamicCapacity;
  }
  validateCapacity(new_capacity);
  ASSERT(validHeaderString(absl::string_view(data, size)));

  if (type() == Type::Reference) {
    // Rather than be too clever and optimize this uncommon case, we switch to
    // Inline mode and copy.
    buffer_ = absl::InlinedVector<char, 128>(BUFFER_STR_VIEW_GET.data(),
                                             BUFFER_STR_VIEW_GET.data() + string_length_);
  }

  BUFFER_IN_VEC_GET.reserve(new_capacity);
  BUFFER_IN_VEC_GET.insert(BUFFER_IN_VEC_GET.begin() + string_length_, data, data + size);
  string_length_ += size;
}

char* HeaderString::buffer() {
  ASSERT(type() == Type::Inline);
  return BUFFER_IN_VEC_GET.data();
}

absl::string_view HeaderString::getStringView() const {
  if (type() == Type::Reference) {
    return BUFFER_STR_VIEW_GET;
  }
  ASSERT(type() == Type::Inline);
  return {BUFFER_IN_VEC_GET.data(), string_length_};
}

void HeaderString::clear() {
  if (type() == Type::Inline) {
    BUFFER_IN_VEC_GET.clear();
    string_length_ = 0;
  }
}

void HeaderString::setCopy(const char* data, uint32_t size) {
  ASSERT(validHeaderString(absl::string_view(data, size)));

  if (!absl::holds_alternative<absl::InlinedVector<char, 128>>(buffer_)) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = absl::InlinedVector<char, 128>();
  }

  BUFFER_IN_VEC_GET.reserve(size);
  BUFFER_IN_VEC_GET.assign(data, data + size);
  string_length_ = size;
  ASSERT(valid());
}

void HeaderString::setCopy(absl::string_view view) {
  this->setCopy(view.data(), static_cast<uint32_t>(view.size()));
}

void HeaderString::setInteger(uint64_t value) {
  const uint32_t max_buffer_length = 32;
  char inner_buffer[max_buffer_length];
  string_length_ = StringUtil::itoa(inner_buffer, max_buffer_length, value);

  if (type() == Type::Reference) {
    // Switching from Type::Reference to Type::Inline
    buffer_ = absl::InlinedVector<char, 128>();
  }
  BUFFER_IN_VEC_GET.assign(inner_buffer, inner_buffer + string_length_);
}

void HeaderString::setReference(absl::string_view ref_value) {
  buffer_ = ref_value;
  string_length_ = ref_value.size();
  ASSERT(valid());
}

// Specialization needed for HeaderMapImpl::HeaderList::insert() when key is LowerCaseString.
// A fully specialized template must be defined once in the program, hence this may not be in
// a header file.
template <> bool HeaderMapImpl::HeaderList::isPseudoHeader(const LowerCaseString& key) {
  return key.get().c_str()[0] == ':';
}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key) : key_(key) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(const LowerCaseString& key, HeaderString&& value)
    : key_(key), value_(std::move(value)) {}

HeaderMapImpl::HeaderEntryImpl::HeaderEntryImpl(HeaderString&& key, HeaderString&& value)
    : key_(std::move(key)), value_(std::move(value)) {}

void HeaderMapImpl::HeaderEntryImpl::value(absl::string_view value) { value_.setCopy(value); }

void HeaderMapImpl::HeaderEntryImpl::value(uint64_t value) { value_.setInteger(value); }

void HeaderMapImpl::HeaderEntryImpl::value(const HeaderEntry& header) {
  value(header.value().getStringView());
}

#define INLINE_HEADER_STATIC_MAP_ENTRY(name)                                                       \
  add(Headers::get().name.get().c_str(), [](HeaderMapImpl& h) -> StaticLookupResponse {            \
    return {&h.inline_headers_.name##_, &Headers::get().name};                                     \
  });

/**
 * This is the static lookup table that is used to determine whether a header is one of the O(1)
 * headers. This uses a trie for lookup time at most equal to the size of the incoming string.
 */
struct HeaderMapImpl::StaticLookupTable : public TrieLookupTable<EntryCb> {
  StaticLookupTable() {
    ALL_INLINE_HEADERS(INLINE_HEADER_STATIC_MAP_ENTRY)

    // Special case where we map a legacy host header to :authority.
    add(Headers::get().HostLegacy.get().c_str(), [](HeaderMapImpl& h) -> StaticLookupResponse {
      return {&h.inline_headers_.Host_, &Headers::get().Host};
    });
  }
};

uint64_t HeaderMapImpl::appendToHeader(HeaderString& header, absl::string_view data,
                                       absl::string_view delimiter) {
  if (data.empty()) {
    return 0;
  }
  uint64_t byte_size = 0;
  if (!header.empty()) {
    header.append(delimiter.data(), delimiter.size());
    byte_size += delimiter.size();
  }
  header.append(data.data(), data.size());
  return data.size() + byte_size;
}

HeaderMapImpl::HeaderMapImpl() { inline_headers_.clear(); }

HeaderMapImpl::HeaderMapImpl(
    const std::initializer_list<std::pair<LowerCaseString, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    HeaderString key_string;
    key_string.setCopy(value.first.get().c_str(), value.first.get().size());
    HeaderString value_string;
    value_string.setCopy(value.second.c_str(), value.second.size());
    addViaMove(std::move(key_string), std::move(value_string));
  }
  verifyByteSize();
}

void HeaderMapImpl::updateSize(uint64_t from_size, uint64_t to_size) {
  ASSERT(cached_byte_size_ >= from_size);
  cached_byte_size_ -= from_size;
  cached_byte_size_ += to_size;
}

void HeaderMapImpl::addSize(uint64_t size) { cached_byte_size_ += size; }

void HeaderMapImpl::subtractSize(uint64_t size) {
  ASSERT(cached_byte_size_ >= size);
  cached_byte_size_ -= size;
}

void HeaderMapImpl::copyFrom(const HeaderMap& header_map) {
  header_map.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // TODO(mattklein123) PERF: Avoid copying here if not necessary.
        HeaderString key_string;
        key_string.setCopy(header.key().getStringView());
        HeaderString value_string;
        value_string.setCopy(header.value().getStringView());

        static_cast<HeaderMapImpl*>(context)->addViaMove(std::move(key_string),
                                                         std::move(value_string));
        return HeaderMap::Iterate::Continue;
      },
      this);
  verifyByteSize();
}

bool HeaderMapImpl::operator==(const HeaderMapImpl& rhs) const {
  if (size() != rhs.size()) {
    return false;
  }

  for (auto i = headers_.begin(), j = rhs.headers_.begin(); i != headers_.end(); ++i, ++j) {
    if (i->key() != j->key().getStringView() || i->value() != j->value().getStringView()) {
      return false;
    }
  }

  return true;
}

bool HeaderMapImpl::operator!=(const HeaderMapImpl& rhs) const { return !operator==(rhs); }

void HeaderMapImpl::insertByKey(HeaderString&& key, HeaderString&& value) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.getStringView());
  if (cb) {
    key.clear();
    StaticLookupResponse ref_lookup_response = cb(*this);
    if (*ref_lookup_response.entry_ == nullptr) {
      maybeCreateInline(ref_lookup_response.entry_, *ref_lookup_response.key_, std::move(value));
    } else {
      const uint64_t added_size =
          appendToHeader((*ref_lookup_response.entry_)->value(), value.getStringView());
      addSize(added_size);
      value.clear();
    }
  } else {
    addSize(key.size() + value.size());
    std::list<HeaderEntryImpl>::iterator i = headers_.insert(std::move(key), std::move(value));
    i->entry_ = i;
  }
}

void HeaderMapImpl::addViaMove(HeaderString&& key, HeaderString&& value) {
  // If this is an inline header, we can't addViaMove, because we'll overwrite
  // the existing value.
  auto* entry = getExistingInline(key.getStringView());
  if (entry != nullptr) {
    const uint64_t added_size = appendToHeader(entry->value(), value.getStringView());
    addSize(added_size);
    key.clear();
    value.clear();
  } else {
    insertByKey(std::move(key), std::move(value));
  }
  verifyByteSize();
}

void HeaderMapImpl::addReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  addViaMove(std::move(ref_key), std::move(ref_value));
  verifyByteSize();
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, uint64_t value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, uint64_t value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    char buf[32];
    StringUtil::itoa(buf, sizeof(buf), value);
    const uint64_t added_size = appendToHeader(entry->value(), buf);
    addSize(added_size);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setInteger(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::addCopy(const LowerCaseString& key, absl::string_view value) {
  auto* entry = getExistingInline(key.get());
  if (entry != nullptr) {
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
    return;
  }
  HeaderString new_key;
  new_key.setCopy(key.get());
  HeaderString new_value;
  new_value.setCopy(value);
  insertByKey(std::move(new_key), std::move(new_value));
  ASSERT(new_key.empty());   // NOLINT(bugprone-use-after-move)
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::appendCopy(const LowerCaseString& key, absl::string_view value) {
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    const uint64_t added_size = appendToHeader(entry->value(), value);
    addSize(added_size);
  } else {
    addCopy(key, value);
  }

  verifyByteSize();
}

void HeaderMapImpl::setReference(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString ref_value(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(ref_value));
  verifyByteSize();
}

void HeaderMapImpl::setReferenceKey(const LowerCaseString& key, absl::string_view value) {
  HeaderString ref_key(key);
  HeaderString new_value;
  new_value.setCopy(value);
  remove(key);
  insertByKey(std::move(ref_key), std::move(new_value));
  ASSERT(new_value.empty()); // NOLINT(bugprone-use-after-move)
  verifyByteSize();
}

void HeaderMapImpl::setCopy(const LowerCaseString& key, absl::string_view value) {
  // Replaces the first occurrence of a header if it exists, otherwise adds by copy.
  // TODO(#9221): converge on and document a policy for coalescing multiple headers.
  auto* entry = getExisting(key);
  if (entry) {
    updateSize(entry->value().size(), value.size());
    entry->value(value);
  } else {
    addCopy(key, value);
  }
  verifyByteSize();
}

uint64_t HeaderMapImpl::byteSize() const { return cached_byte_size_; }

uint64_t HeaderMapImpl::byteSizeInternal() const {
  // Computes the total byte size by summing the byte size of the keys and values.
  uint64_t byte_size = 0;
  for (const HeaderEntryImpl& header : headers_) {
    byte_size += header.key().size();
    byte_size += header.value().size();
  }
  return byte_size;
}

const HeaderEntry* HeaderMapImpl::get(const LowerCaseString& key) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      return &header;
    }
  }

  return nullptr;
}

HeaderEntry* HeaderMapImpl::getExisting(const LowerCaseString& key) {
  for (HeaderEntryImpl& header : headers_) {
    if (header.key() == key.get().c_str()) {
      return &header;
    }
  }

  return nullptr;
}

void HeaderMapImpl::iterate(ConstIterateCb cb, void* context) const {
  for (const HeaderEntryImpl& header : headers_) {
    if (cb(header, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

void HeaderMapImpl::iterateReverse(ConstIterateCb cb, void* context) const {
  for (auto it = headers_.rbegin(); it != headers_.rend(); it++) {
    if (cb(*it, context) == HeaderMap::Iterate::Break) {
      break;
    }
  }
}

HeaderMap::Lookup HeaderMapImpl::lookup(const LowerCaseString& key,
                                        const HeaderEntry** entry) const {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    // The accessor callbacks for predefined inline headers take a HeaderMapImpl& as an argument;
    // even though we don't make any modifications, we need to cast_cast in order to use the
    // accessor.
    //
    // Making this work without const_cast would require managing an additional const accessor
    // callback for each predefined inline header and add to the complexity of the code.
    StaticLookupResponse ref_lookup_response = cb(const_cast<HeaderMapImpl&>(*this));
    *entry = *ref_lookup_response.entry_;
    if (*entry) {
      return Lookup::Found;
    } else {
      return Lookup::NotFound;
    }
  } else {
    *entry = nullptr;
    return Lookup::NotSupported;
  }
}

void HeaderMapImpl::clear() {
  inline_headers_.clear();
  headers_.clear();
  cached_byte_size_ = 0;
}

void HeaderMapImpl::remove(const LowerCaseString& key) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key.get());
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    removeInline(ref_lookup_response.entry_);
  } else {
    for (auto i = headers_.begin(); i != headers_.end();) {
      if (i->key() == key.get().c_str()) {
        subtractSize(i->key().size() + i->value().size());
        i = headers_.erase(i);
      } else {
        ++i;
      }
    }
  }
  verifyByteSize();
}

void HeaderMapImpl::removePrefix(const LowerCaseString& prefix) {
  headers_.remove_if([&prefix, this](const HeaderEntryImpl& entry) {
    bool to_remove = absl::StartsWith(entry.key().getStringView(), prefix.get());
    if (to_remove) {
      // If this header should be removed, make sure any references in the
      // static lookup table are cleared as well.
      EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(entry.key().getStringView());
      if (cb) {
        StaticLookupResponse ref_lookup_response = cb(*this);
        if (ref_lookup_response.entry_) {
          const uint32_t key_value_size = (*ref_lookup_response.entry_)->key().size() +
                                          (*ref_lookup_response.entry_)->value().size();
          subtractSize(key_value_size);
          *ref_lookup_response.entry_ = nullptr;
        }
      } else {
        subtractSize(entry.key().size() + entry.value().size());
      }
    }
    return to_remove;
  });
  verifyByteSize();
}

void HeaderMapImpl::dumpState(std::ostream& os, int indent_level) const {
  using IterateData = std::pair<std::ostream*, const char*>;
  const char* spaces = spacesForLevel(indent_level);
  IterateData iterate_data = std::make_pair(&os, spaces);
  iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto* data = static_cast<IterateData*>(context);
        *data->first << data->second << "'" << header.key().getStringView() << "', '"
                     << header.value().getStringView() << "'\n";
        return HeaderMap::Iterate::Continue;
      },
      &iterate_data);
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key) {
  if (*entry) {
    return **entry;
  }

  addSize(key.get().size());
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key);
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl& HeaderMapImpl::maybeCreateInline(HeaderEntryImpl** entry,
                                                                 const LowerCaseString& key,
                                                                 HeaderString&& value) {
  if (*entry) {
    value.clear();
    return **entry;
  }

  addSize(key.get().size() + value.size());
  std::list<HeaderEntryImpl>::iterator i = headers_.insert(key, std::move(value));
  i->entry_ = i;
  *entry = &(*i);
  return **entry;
}

HeaderMapImpl::HeaderEntryImpl* HeaderMapImpl::getExistingInline(absl::string_view key) {
  EntryCb cb = ConstSingleton<StaticLookupTable>::get().find(key);
  if (cb) {
    StaticLookupResponse ref_lookup_response = cb(*this);
    return *ref_lookup_response.entry_;
  }
  return nullptr;
}

void HeaderMapImpl::removeInline(HeaderEntryImpl** ptr_to_entry) {
  if (!*ptr_to_entry) {
    return;
  }

  HeaderEntryImpl* entry = *ptr_to_entry;
  const uint64_t size_to_subtract = entry->entry_->key().size() + entry->entry_->value().size();
  subtractSize(size_to_subtract);
  *ptr_to_entry = nullptr;
  headers_.erase(entry->entry_);
  verifyByteSize();
}

} // namespace Http
} // namespace Envoy
