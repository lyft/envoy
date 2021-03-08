#pragma once

#include "envoy/filesystem/filesystem.h"
#include "common/filesystem/file_shared_impl.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {

namespace Filesystem {

struct MemFileInfo {
  absl::Mutex lock_;
  std::string data_ GUARDED_BY(lock_);
};

class MemfileImpl : public FileSharedImpl {
public:
  MemfileImpl(const std::string& path, std::shared_ptr<MemFileInfo>& info) : FileSharedImpl(path), info_(info) {
  }

protected:
  Api::IoCallBoolResult open(FlagSet flag) override {
    ASSERT(!isOpen());
    flags_ = flag;
    fd_++;
    return resultSuccess(true);
  }

  Api::IoCallSizeResult write(absl::string_view buffer) override {
    absl::MutexLock l(&info_->lock_);
    if (!flags_.test(File::Operation::Append)) {
      info_->data_.clear();
    }
    info_->data_.append(buffer);
    const ssize_t size = info_->data_.size();
    return resultSuccess(size);
  }

  Api::IoCallBoolResult close() override {
    ASSERT(isOpen());
    fd_--;
    return resultSuccess(true);
  }

private:
  FlagSet flags_;
  std::shared_ptr<MemFileInfo> info_;
};

class MemfileInstanceImpl : public Instance {
public:
  MemfileInstanceImpl();

  FilePtr createFile(const std::string& path) override {
    //absl::MutexLock m(&lock_);
    //if (file_system_->fileExists(path) || !use_memfiles_) {
      return file_system_->createFile(path);
    /*}
    auto it = files_.find(path);
    if (it == files_.end()) {
      it = files_.emplace(path, std::make_shared<MemFileInfo>()).first;
    }
    std::shared_ptr<MemFileInfo> info = it->second;
    return FilePtr{new MemfileImpl(path, info)};*/
  }

  bool fileExists(const std::string& path) override {
    return file_system_->fileExists(path);
/*    absl::MutexLock m(&lock_);
    auto it = files_.find(path);
    return (it != files_.end() || file_system_->fileExists(path));*/
  }

  bool directoryExists(const std::string& path) override {
    return file_system_->directoryExists(path);
  }

  ssize_t fileSize(const std::string& path) override {
/*    {
      absl::MutexLock m(&lock_);
      auto it = files_.find(path);
      if (it != files_.end()) {
        ASSERT(use_memfiles_);
        absl::MutexLock n(&it->second->lock_);
        return it->second->data_.size();
      }
    }*/
    return file_system_->fileSize(path);
  }

  std::string fileReadToEnd(const std::string& path) override {
/*    {
      absl::MutexLock m(&lock_);
      auto it = files_.find(path);
      if (it != files_.end()) {
        absl::MutexLock n(&it->second->lock_);
        ASSERT(use_memfiles_);
        return it->second->data_;
      }
    }*/
    return file_system_->fileReadToEnd(path);
  }

  PathSplitResult splitPathFromFilename(absl::string_view path) override {
    return file_system_->splitPathFromFilename(path);
  }

  bool illegalPath(const std::string& path) override {
    return file_system_->illegalPath(path);
  }

  void renameFile(const std::string& old_name, const std::string& new_name);
  void setUseMemfiles(bool value) {
      absl::MutexLock m(&lock_);
    use_memfiles_ = value;
  }

private:
  std::unique_ptr<Instance> file_system_;
  absl::Mutex lock_;
  bool use_memfiles_ ABSL_GUARDED_BY(lock_);
  absl::flat_hash_map<std::string, std::shared_ptr<MemFileInfo>> files_ ABSL_GUARDED_BY(lock_);
};

MemfileInstanceImpl& fileSystemForTest();

} // namespace Filesystem

} // namespace Envoy
