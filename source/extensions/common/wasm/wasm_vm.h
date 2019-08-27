#pragma once

#include <memory>

#include "envoy/common/exception.h"

#include "common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class Context;

// Represents a WASM-native word-sized datum. On 32-bit VMs, the high bits are always zero.
// The WASM/VM API treats all bits as significant.
struct Word {
  Word(uint64_t w) : u64_(w) {} // Implicit conversion into Word.
  uint32_t u32() const { return static_cast<uint32_t>(u64_); }
  uint64_t u64_;
};

// Convert Word type for use by 32-bit VMs.
template <typename T> struct ConvertWordTypeToUint32 { using type = T; };
template <> struct ConvertWordTypeToUint32<Word> { using type = uint32_t; };

// Convert Word-based function types for 32-bit VMs.
template <typename F> struct ConvertFunctionTypeWordToUint32 {};
template <typename R, typename... Args> struct ConvertFunctionTypeWordToUint32<R (*)(Args...)> {
  using type = typename ConvertWordTypeToUint32<R>::type (*)(
      typename ConvertWordTypeToUint32<Args>::type...);
};

// A wrapper for a global variable within the VM.
template <typename T> struct Global {
  virtual ~Global() = default;
  virtual T get() PURE;
  virtual void set(const T& t) PURE;
};

// Calls into the WASM VM.
// 1st arg is always a pointer to Context (Context*).
using WasmCall0Void = std::function<void(Context*)>;
using WasmCall1Void = std::function<void(Context*, Word)>;
using WasmCall2Void = std::function<void(Context*, Word, Word)>;
using WasmCall3Void = std::function<void(Context*, Word, Word, Word)>;
using WasmCall4Void = std::function<void(Context*, Word, Word, Word, Word)>;
using WasmCall5Void = std::function<void(Context*, Word, Word, Word, Word, Word)>;
using WasmCall6Void = std::function<void(Context*, Word, Word, Word, Word, Word, Word)>;
using WasmCall7Void = std::function<void(Context*, Word, Word, Word, Word, Word, Word, Word)>;
using WasmCall8Void = std::function<void(Context*, Word, Word, Word, Word, Word, Word, Word, Word)>;
using WasmCall0Word = std::function<Word(Context*)>;
using WasmCall1Word = std::function<Word(Context*, Word)>;
using WasmCall2Word = std::function<Word(Context*, Word, Word)>;
using WasmCall3Word = std::function<Word(Context*, Word, Word, Word)>;
using WasmCall4Word = std::function<Word(Context*, Word, Word, Word, Word)>;
using WasmCall5Word = std::function<Word(Context*, Word, Word, Word, Word, Word)>;
using WasmCall6Word = std::function<Word(Context*, Word, Word, Word, Word, Word, Word)>;
using WasmCall7Word = std::function<Word(Context*, Word, Word, Word, Word, Word, Word, Word)>;
using WasmCall8Word = std::function<Word(Context*, Word, Word, Word, Word, Word, Word, Word, Word)>;
#define FOR_ALL_WASM_VM_EXPORTS(_f)                                                                \
  _f(WasmCall0Void) _f(WasmCall1Void) _f(WasmCall2Void) _f(WasmCall3Void) _f(WasmCall4Void)        \
      _f(WasmCall5Void) _f(WasmCall8Void) _f(WasmCall0Word) _f(WasmCall1Word) _f(WasmCall3Word)

// Calls out of the WASM VM.
// 1st arg is always a pointer to raw_context (void*).
using WasmCallback0Void = void (*)(void*);
using WasmCallback1Void = void (*)(void*, Word);
using WasmCallback2Void = void (*)(void*, Word, Word);
using WasmCallback3Void = void (*)(void*, Word, Word, Word);
using WasmCallback4Void = void (*)(void*, Word, Word, Word, Word);
using WasmCallback5Void = void (*)(void*, Word, Word, Word, Word, Word);
using WasmCallback6Void = void (*)(void*, Word, Word, Word, Word, Word, Word);
using WasmCallback7Void = void (*)(void*, Word, Word, Word, Word, Word, Word, Word);
using WasmCallback8Void = void (*)(void*, Word, Word, Word, Word, Word, Word, Word, Word);
using WasmCallback0Word = Word (*)(void*);
using WasmCallback1Word = Word (*)(void*, Word);
using WasmCallback2Word = Word (*)(void*, Word, Word);
using WasmCallback3Word = Word (*)(void*, Word, Word, Word);
using WasmCallback4Word = Word (*)(void*, Word, Word, Word, Word);
using WasmCallback5Word = Word (*)(void*, Word, Word, Word, Word, Word);
using WasmCallback6Word = Word (*)(void*, Word, Word, Word, Word, Word, Word);
using WasmCallback7Word = Word (*)(void*, Word, Word, Word, Word, Word, Word, Word, Word);
using WasmCallback8Word = Word (*)(void*, Word, Word, Word, Word, Word, Word, Word, Word, Word);
using WasmCallback9Word = Word (*)(void*, Word, Word, Word, Word, Word, Word, Word, Word, Word,
                                   Word);
#define FOR_ALL_WASM_VM_IMPORTS(_f)                                                                \
  _f(WasmCallback0Void) _f(WasmCallback1Void) _f(WasmCallback2Void) _f(WasmCallback3Void)          \
      _f(WasmCallback4Void) _f(WasmCallback0Word) _f(WasmCallback1Word) _f(WasmCallback2Word)      \
          _f(WasmCallback3Word) _f(WasmCallback4Word) _f(WasmCallback5Word) _f(WasmCallback6Word)  \
              _f(WasmCallback7Word) _f(WasmCallback8Word) _f(WasmCallback9Word)                    \
                  _f(WasmCallback_WWl) _f(WasmCallback_WWm)

// Using the standard g++/clang mangling algorithm:
// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-builtin
// Extended with W = Word
// Z = void, j = uint32_t, l = int64_t, m = uint64_t
using WasmCallback_WWl = Word (*)(void*, Word, int64_t);
using WasmCallback_WWm = Word (*)(void*, Word, uint64_t);

// Wasm VM instance. Provides the low level WASM interface.
class WasmVm : public Logger::Loggable<Logger::Id::wasm> {
public:
  using WasmVmPtr = std::unique_ptr<WasmVm>;

  virtual ~WasmVm() = default;
  /**
   * Return the VM identifier.
   * @return one of WasmVmValues from well_known_names.h e.g. "envoy.wasm.vm.null".
   */
  virtual absl::string_view vm() PURE;

  /**
   * Whether or not the VM implementation supports cloning.
   * @return true if the VM is clone-able.
   */
  virtual bool clonable() PURE;

  /**
   * Make a thread-specific copy. This may not be supported by the underlying VM system in which
   * case it will return nullptr and the caller will need to create a new VM from scratch.
   * @return a clone of 'this' (e.g. for a different Worker/thread).
   */
  virtual WasmVmPtr clone() PURE;
  /**
   * Load the WASM code from a file. Return true on success.
   * @param code the WASM binary code (or registered NullVm plugin name).
   * @param allow_precompiled if true, allows supporting VMs (e.g. WAVM) to load the binary
   * machine code from a user-defined section of the WASM file.
   * @return whether or not the load was successful.
   */
  virtual bool load(const std::string& code, bool allow_precompiled) PURE;
  /**
   * Link to registered function.
   * @param debug_name user-provided name for use in error messages.
   * @param needs_emscripten whether emscripten support should be provided (e.g.
   * _emscripten_memcpy_bigHandler).
   */
  virtual void link(absl::string_view debug_name, bool needs_emscripten) PURE;

  /**
   * Set memory layout (start of dynamic heap base, etc.) in the VM.
   * @param stack_base the location in VM memory of the stack.
   * @param heap_base the location in VM memory of the heap.
   * @param heap_base_ptr the location in VM memory of a location to store the heap pointer.
   */
  virtual void setMemoryLayout(uint64_t stack_base, uint64_t heap_base,
                               uint64_t heap_base_pointer) PURE;

  /**
   * Call the 'start' function and initialize globals.
   * @param vm_context a context which represents the caller: in this case Envoy itself.
   */
  virtual void start(Context* vm_context) PURE;

  /**
   * Get size of the currently allocated memory in the VM.
   * @return the size of memory in bytes.
   */
  virtual uint64_t getMemorySize() PURE;
  /**
   * Convert a block of memory in the VM to a string_view.
   * @param pointer the offset into VM memory of the requested VM memory block.
   * @param size the size of the requested VM memory block.
   * @return if std::nullopt then the pointer/size pair were invalid, otherwise returns
   * a host string_view pointing to the pointer/size pair in VM memory.
   */
  virtual absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) PURE;
  /**
   * Convert a host pointer to memory in the VM into a VM "pointer" (an offset into the Memory).
   * @param host_pointer a pointer to host memory to be converted into a VM offset (pointer).
   * @param vm_pointer a pointer to an uint64_t to be filled with the offset in VM memory
   * corresponding to 'host_pointer'.
   * @return whether or not the host_pointer was a valid VM memory offset.
   */
  virtual bool getMemoryOffset(void* host_pointer, uint64_t* vm_pointer) PURE;
  /**
   * Set a block of memory in the VM, returns true on success, false if the pointer/size is invalid.
   * @param pointer the offset into VM memory describing the start of a region of VM memory.
   * @param size the size of the region of VM memory.
   * @return whether or not the pointer/size pair was a valid VM memory block.
   */
  virtual bool setMemory(uint64_t pointer, uint64_t size, const void* data) PURE;
  /**
   * Set a Word in the VM, returns true on success, false if the pointer is invalid.
   * @param pointer the offset into VM memory describing the start of VM native word size block.
   * @param data a Word whose contents will be written in VM native word size at 'pointer.
   * @return whether or not the pointer was to a valid VM memory block of VM native word size.
   */
  virtual bool setWord(uint64_t pointer, Word data) PURE;
  /**
   * Make a new intrinsic module (e.g. for Emscripten support).
   * @param name the name of the module to make.
   */
  virtual void makeModule(absl::string_view name) PURE;

  /**
   * Get the contents of the user section with the given name or "" if it does not exist.
   * @param name the name of the user section to get.
   * @return the contents of the user section (if any).  The result will be empty() if there
   * is no such section.
   */
  virtual absl::string_view getUserSection(absl::string_view name) PURE;

  /**
   * Get typed function exported by the WASM module.
   */
#define _GET_FUNCTION(_T) virtual void getFunction(absl::string_view functionName, _T* f) PURE;
  FOR_ALL_WASM_VM_EXPORTS(_GET_FUNCTION)
#undef _GET_FUNCTION

  /**
   * Register typed callbacks exported by the host environment.
   */
#define _REGISTER_CALLBACK(_T)                                                                     \
  virtual void registerCallback(absl::string_view moduleName, absl::string_view functionName,      \
                                _T f, typename ConvertFunctionTypeWordToUint32<_T>::type) PURE;
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  /**
   * Register typed value exported by the host environment.
   * @param module_name the name of the module to which to export the global.
   * @param name the name of the global variable to export.
   * @param initial_value the initial value of the global.
   * @return a Global object which can be used to access the exported global.
   */
  virtual std::unique_ptr<Global<Word>> makeGlobal(absl::string_view module_name,
                                                   absl::string_view name, Word initial_value) PURE;
  /**
   * Register typed value exported by the host environment.
   * @param module_name the name of the module to which to export the global.
   * @param name the name of the global variable to export.
   * @param initial_value the initial value of the global.
   * @return a Global object which can be used to access the exported global.
   */
  virtual std::unique_ptr<Global<double>>
  makeGlobal(absl::string_view module_name, absl::string_view name, double initial_value) PURE;
};
using WasmVmPtr = std::unique_ptr<WasmVm>;

// Exceptions for issues with the WasmVm.
class WasmVmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

// Exceptions for issues with the WebAssembly code.
class WasmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

// Thread local state set during a call into a WASM VM so that calls coming out of the
// VM can be attributed correctly to calling Filter. We use thread_local instead of ThreadLocal
// because this state is live only during the calls and does not need to be initialized consistently
// over all workers as with ThreadLocal data.
extern thread_local Envoy::Extensions::Common::Wasm::Context* current_context_;
// Requested effective context set by code within the VM to request that the calls coming out of the
// VM be attributed to another filter, for example if a control plane gRPC comes back to the
// RootContext which effects some set of waiting filters.
extern thread_local uint32_t effective_context_id_;

// Helper to save and restore thread local VM call context information to support reentrant calls.
// NB: this happens for example when a call from the VM invokes a handler which needs to _malloc
// memory in the VM.
struct SaveRestoreContext {
  explicit SaveRestoreContext(Context* context) {
    saved_context = current_context_;
    saved_effective_context_id_ = effective_context_id_;
    current_context_ = context;
    effective_context_id_ = 0; // No effective context id.
  }
  ~SaveRestoreContext() {
    current_context_ = saved_context;
    effective_context_id_ = saved_effective_context_id_;
  }
  Context* saved_context;
  uint32_t saved_effective_context_id_;
};

// Create a new low-level WASM VM of the give type (e.g. "envoy.wasm.vm.wavm").
WasmVmPtr createWasmVm(absl::string_view vm);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
