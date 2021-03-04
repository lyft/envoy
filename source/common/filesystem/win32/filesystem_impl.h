#pragma once

#include <cstdint>
#include <string>

#include "common/filesystem/file_shared_impl.h"

namespace Envoy {
namespace Filesystem {

class FileImplWin32 : public FileSharedImpl {
public:
  FileImplWin32(const FilePathAndType& file_info) : FileSharedImpl(file_info) {}
  ~FileImplWin32();

protected:
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  struct FlagsAndMode {
    DWORD access_ = 0;
    DWORD creation_ = 0;
  };

  FlagsAndMode translateFlag(FlagSet in);

private:
  friend class FileSystemImplTest;
};

struct ConsoleFileImplWin32 : public FileImplWin32 {
  ConsoleFileImplWin32() : FileImplWin32(FilePathAndType{DestinationType::Console, "CONOUT$"}) {}

protected:
  Api::IoCallBoolResult open(FlagSet flag) override;
};

template <DWORD std_handle_> struct StdStreamFileImplWin32 : public FileImplWin32 {
  static_assert(std_handle_ == STD_OUTPUT_HANDLE || std_handle_ == STD_ERROR_HANDLE);
  StdStreamFileImplWin32() : FileImplWin32(StdStreamFileImplWin32::fromStdHandle()) {}
  ~StdStreamFileImplWin32() { fd_ = INVALID_HANDLE; }

  Api::IoCallBoolResult open(FlagSet) {
    fd_ = GetStdHandle(std_handle_);
    if (fd_ == NULL) {
      // If an application does not have associated standard handles,
      // such as a service running on an interactive desktop
      // and has not redirected them, the return value is NULL.
      return resultFailure(false, INVALID_HANDLE);
    }
    if (fd_ == INVALID_HANDLE) {
      return resultFailure(false, ::GetLastError());
    }
    return resultSuccess(true);
  }

  Api::IoCallBoolResult close() {
    // If we are writing to the standard output of the process we are
    // not the owners of the handle, we are just using it.
    fd_ = INVALID_HANDLE;
    return resultSuccess(true);
  }

  static constexpr FilePathAndType fromStdHandle() {
    if constexpr (std_handle_ == STD_OUTPUT_HANDLE) {
      return FilePathAndType{DestinationType::Stdout, "/dev/stdout"};
    } else {
      return FilePathAndType{DestinationType::Stderr, "/dev/stderr"};
    }
  }
};

class InstanceImplWin32 : public Instance {
public:
  // Filesystem::Instance
  FilePtr createFile(const FilePathAndType& file_info) override;
  FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;
};

} // namespace Filesystem
} // namespace Envoy
