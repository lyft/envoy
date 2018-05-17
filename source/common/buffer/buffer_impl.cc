#include "common/buffer/buffer_impl.h"

#include <cstdint>
#include <string>

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"

#include "event2/buffer.h"

namespace Envoy {
namespace Buffer {

// RawSlice is the same structure as evbuffer_iovec. This was put into place to avoid leaking
// libevent into most code since we will likely replace evbuffer with our own implementation at
// some point. However, we can avoid a bunch of copies since the structure is the same.
static_assert(sizeof(RawSlice) == sizeof(evbuffer_iovec), "RawSlice != evbuffer_iovec");
static_assert(offsetof(RawSlice, mem_) == offsetof(evbuffer_iovec, iov_base),
              "RawSlice != evbuffer_iovec");
static_assert(offsetof(RawSlice, len_) == offsetof(evbuffer_iovec, iov_len),
              "RawSlice != evbuffer_iovec");

void OwnedImpl::add(const void* data, uint64_t size) { evbuffer_add(buffer_.get(), data, size); }

void OwnedImpl::addBufferFragment(BufferFragment& fragment) {
  evbuffer_add_reference(
      buffer_.get(), fragment.data(), fragment.size(),
      [](const void*, size_t, void* arg) { static_cast<BufferFragment*>(arg)->done(); }, &fragment);
}

void OwnedImpl::add(const std::string& data) {
  evbuffer_add(buffer_.get(), data.c_str(), data.size());
}

void OwnedImpl::add(const Instance& data) {
  uint64_t num_slices = data.getRawSlices(nullptr, 0);
  RawSlice slices[num_slices];
  data.getRawSlices(slices, num_slices);
  for (RawSlice& slice : slices) {
    add(slice.mem_, slice.len_);
  }
}

void OwnedImpl::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  int rc =
      evbuffer_commit_space(buffer_.get(), reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
  ASSERT(rc == 0);
}

void OwnedImpl::copyOut(size_t start, uint64_t size, void* data) const {
  ASSERT(start + size <= length());

  evbuffer_ptr start_ptr;
  int rc = evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET);
  ASSERT(rc != -1);

  ev_ssize_t copied = evbuffer_copyout_from(buffer_.get(), &start_ptr, data, size);
  ASSERT(static_cast<uint64_t>(copied) == size);
}

void OwnedImpl::drain(uint64_t size) {
  ASSERT(size <= length());
  int rc = evbuffer_drain(buffer_.get(), size);
  ASSERT(rc == 0);
}

uint64_t OwnedImpl::getRawSlices(RawSlice* out, uint64_t out_size) const {
  return evbuffer_peek(buffer_.get(), -1, nullptr, reinterpret_cast<evbuffer_iovec*>(out),
                       out_size);
}

uint64_t OwnedImpl::length() const { return evbuffer_get_length(buffer_.get()); }

void* OwnedImpl::linearize(uint32_t size) {
  ASSERT(size <= length());
  return evbuffer_pullup(buffer_.get(), size);
}

void OwnedImpl::move(Instance& rhs) {
  // We do the static cast here because in practice we only have one buffer implementation right
  // now and this is safe. Using the evbuffer move routines require having access to both evbuffers.
  // This is a reasonable compromise in a high performance path where we want to maintain an
  // abstraction in case we get rid of evbuffer later.
  int rc = evbuffer_add_buffer(buffer_.get(), static_cast<LibEventInstance&>(rhs).buffer().get());
  ASSERT(rc == 0);
  static_cast<LibEventInstance&>(rhs).postProcess();
}

void OwnedImpl::move(Instance& rhs, uint64_t length) {
  // See move() above for why we do the static cast.
  int rc = evbuffer_remove_buffer(static_cast<LibEventInstance&>(rhs).buffer().get(), buffer_.get(),
                                  length);
  ASSERT(static_cast<uint64_t>(rc) == length);
  static_cast<LibEventInstance&>(rhs).postProcess();
}

int OwnedImpl::read(int fd, uint64_t max_length) {
  if (max_length == 0) {
    return 0;
  }
  constexpr uint64_t MaxSlices = 2;
  RawSlice slices[MaxSlices];
  uint64_t num_slices = reserve(max_length, slices, MaxSlices);
  struct iovec iov[num_slices];
  int num_iov = 0;
  for (uint64_t i = 0; i < num_slices && max_length != 0; i++) {
    num_iov++;
    iov[i].iov_base = slices[i].mem_;
    iov[i].iov_len = std::min(slices[i].len_, size_t(max_length));
    max_length -= slices[i].len_;
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const ssize_t rc = os_syscalls.readv(fd, iov, num_iov);
  if (rc < 0) {
    return rc;
  }
  uint64_t num_slices_to_commit = 0;
  uint64_t bytes_to_commit = rc;
  while (bytes_to_commit != 0) {
    if (slices[num_slices_to_commit].len_ > bytes_to_commit) {
      slices[num_slices_to_commit].len_ = bytes_to_commit;
    }
    bytes_to_commit -= slices[num_slices_to_commit].len_;
    num_slices_to_commit++;
  }
  ASSERT(num_slices_to_commit <= num_slices);
  commit(slices, num_slices_to_commit);
  return rc;
}

uint64_t OwnedImpl::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  uint64_t ret = evbuffer_reserve_space(buffer_.get(), length,
                                        reinterpret_cast<evbuffer_iovec*>(iovecs), num_iovecs);
  ASSERT(ret >= 1);
  return ret;
}

ssize_t OwnedImpl::search(const void* data, uint64_t size, size_t start) const {
  evbuffer_ptr start_ptr;
  if (-1 == evbuffer_ptr_set(buffer_.get(), &start_ptr, start, EVBUFFER_PTR_SET)) {
    return -1;
  }

  evbuffer_ptr result_ptr =
      evbuffer_search(buffer_.get(), static_cast<const char*>(data), size, &start_ptr);
  return result_ptr.pos;
}

int OwnedImpl::write(int fd) {
  constexpr uint64_t MaxSlices = 16;
  RawSlice slices[MaxSlices];
  uint64_t num_slices = std::min(getRawSlices(slices, MaxSlices), MaxSlices);
  if (num_slices == 0) {
    return 0;
  }
  struct iovec iov[num_slices];
  uint64_t j = 0;
  for (uint64_t i = 0; i < num_slices; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[j].iov_base = slices[i].mem_;
      iov[j].iov_len = slices[i].len_;
      j++;
    }
  }
  if (j == 0) {
    return 0;
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  ssize_t rc = os_syscalls.writev(fd, iov, j);
  if (rc > 0) {
    drain(uint64_t(rc));
  }
  return int(rc);
}

OwnedImpl::OwnedImpl() : buffer_(evbuffer_new()) {}

OwnedImpl::OwnedImpl(const std::string& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const Instance& data) : OwnedImpl() { add(data); }

OwnedImpl::OwnedImpl(const void* data, uint64_t size) : OwnedImpl() { add(data, size); }

} // namespace Buffer
} // namespace Envoy
