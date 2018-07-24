#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadata {
public:
  virtual ~DynamicMetadata(){};

  /**
   * @param data_name the name of the data being set.  
   * @param data an owning pointer to the data to be stored.
   * Note that it is an error to call setData() twice with the same data_name.
   */
  template <typename T> void setData(absl::string_view data_name, std::unique_ptr<T>&& data) {
    setDataGeneric(data_name, Traits<T>::getTypeId(), static_cast<void*>(data.release()),
                   &Traits<T>::destructor);
  }

  /**
   * @param data_name the name of the data being set.  
   * @return a reference to the stored data.
   * Note that it is an error to access data that has not previously been set.
   */
  template <typename T> const T& getData(absl::string_view data_name) {
    return *static_cast<T*>(getDataGeneric(data_name, Traits<T>::getTypeId()));
  }

  template <typename T> bool hasData(absl::string_view data_name) {
    return hasDataGeneric(data_name, Traits<T>::getTypeId());
  }

protected:
  virtual void setDataGeneric(absl::string_view data_name, size_t type_id,
                              void* data, // Implementation must take ownership
                              void (*destructor)(void*)) PURE;

  virtual void* getDataGeneric(absl::string_view data_name, size_t type_id) PURE;
  virtual bool hasDataGeneric(absl::string_view data_name, size_t type_id) PURE;

private:
  static size_t type_id_index_;

  template <typename T> class Traits {
  public:
    static size_t getTypeId() {
      static const size_t type_id = ++type_id_index_;
      return type_id;
    }
    static void destructor(void* ptr) { delete static_cast<T*>(ptr); }
  };
};

} // namespace RequestInfo
} // namespace Envoy
