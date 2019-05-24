// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_storage_impl.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "/usr/local/google/home/danzh/.cache/bazel/_bazel_danzh/3af5f831530d3ae92cc2833051a9b35d/execroot/envoy/bazel-out/k8-fastbuild/bin/source/common/buffer/_virtual_includes/buffer_lib/common/buffer/buffer_impl.h"
#include "quiche/quic/core/quic_utils.h"

namespace quic {

// TODO(danzh)Note that |allocator| is not used to allocate memory currently, instead,
// Buffer::OwnedImpl allocates memory on its own. Investigate if a customized
// QuicBufferAllocator can improve cache hit.
QuicMemSliceStorageImpl::QuicMemSliceStorageImpl(const struct iovec* iov, int iov_count,
                                                 QuicBufferAllocator* /*allocator*/,
                                                 const QuicByteCount max_slice_len) {
  if (iov == nullptr) {
    return;
  }
  QuicByteCount write_len = 0;
  for (int i = 0; i < iov_count; ++i) {
    write_len += iov[i].iov_len;
  }
  size_t io_offset = 0;
  while (io_offset < write_len) {
    size_t slice_len = std::min(write_len - io_offset, max_slice_len);
    Envoy::Buffer::RawSlice slice;
    // Populate a temporary buffer instance and then move it to |buffer_|.
    // This is necessary for the old evbuffer implementation of OwnedImpl where
    // consecutive reserve/commit can return addresses in same slice which
    // violates the restriction of |max_slice_len| when ToSpan() is called.
    Envoy::Buffer::OwnedImpl buffer;
    uint16_t num_slice = buffer.reserve(slice_len, &slice, 1);
    ASSERT(num_slice == 1);
    QuicUtils::CopyToBuffer(iov, iov_count, io_offset, slice_len, static_cast<char*>(slice.mem_));
    io_offset += slice_len;
    // evbuffer may return a slice longer than needed, trim it to requested length.
    slice.len_ = slice_len;
    buffer.commit(&slice, num_slice);
    buffer_.move(buffer);
  }
}

} // namespace quic
