#pragma once

#include <core_net/vm/allocator.hpp>
#include <core_net/vm/constants.hpp>
#include <core_net/vm/exceptions.hpp>
#include <core_net/vm/execution_interface.hpp>
#include <core_net/vm/host_function.hpp>
#include <core_net/vm/opcodes.hpp>
#include <core_net/vm/signals.hpp>
#include <core_net/vm/types.hpp>
#include <core_net/vm/utils.hpp>
#include <core_net/vm/wasm_stack.hpp>

#include <algorithm>
#include <cassert>
#include <signal.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

// OSX requires _XOPEN_SOURCE to #include <ucontext.h>
#ifdef __APPLE__
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif
#include <ucontext.h>

namespace core_net { namespace vm {

   struct null_host_functions {
      template<typename... A>
      void operator()(A&&...) const {
         CORE_NET_VM_ASSERT(false, wasm_interpreter_exception,
                       "Should never get here because it's impossible to link a module "
                       "that imports any host functions, when no host functions are available");
      }
   };

   namespace detail {
      template <typename HostFunctions>
      struct host_type {
         using type = typename HostFunctions::host_type_t;
      };
      template <>
      struct host_type<std::nullptr_t> {
         using type = std::nullptr_t;
      };

      template <typename HF>
      using host_type_t = typename host_type<HF>::type;

      template <typename HostFunctions>
      struct type_converter {
         using type = typename HostFunctions::type_converter_t;
      };
      template <>
      struct type_converter<std::nullptr_t> {
         using type = core_net::vm::type_converter<std::nullptr_t, core_net::vm::execution_interface>;
      };

      template <typename HF>
      using type_converter_t = typename type_converter<HF>::type;

      template <typename HostFunctions>
      struct host_invoker {
         using type = HostFunctions;
      };
      template <>
      struct host_invoker<std::nullptr_t> {
         using type = null_host_functions;
      };
      template <typename HF>
      using host_invoker_t = typename host_invoker<HF>::type;
   }

   template<typename Derived, typename Host, bool IsJit>
   class execution_context_base {
      using host_type  = detail::host_type_t<Host>;
    public:
      Derived& derived() { return static_cast<Derived&>(*this); }
      execution_context_base() {}
      execution_context_base(module* m) : _mod(m) {}

      inline void initialize_globals() {
         if constexpr (IsJit) {
            return initialize_globals_impl(*_mod->jit_mod);
         }
         else {
            return initialize_globals_impl(*_mod);
         }
      }

      template<typename Module>
      inline void initialize_globals_impl(const Module& mod) {
         CORE_NET_VM_ASSERT(_globals.empty(), wasm_memory_exception, "initialize_globals called on non-empty _globals");
         _globals.reserve(mod.globals.size());
         for (uint32_t i = 0; i < mod.globals.size(); i++) {
            _globals.emplace_back(mod.globals[i].init);
         }
      }

      inline int32_t grow_linear_memory(int32_t pages) {
         if constexpr (IsJit) {
            return grow_linear_memory_impl(*_mod->jit_mod, pages);
         } else {
            return grow_linear_memory_impl(*_mod, pages);
         }
      }

      template<typename Module>
      inline int32_t grow_linear_memory_impl(const Module& mod, int32_t pages) {
         const int32_t sz = _wasm_alloc->get_current_page();
         if (pages < 0) {
            if (sz + pages < 0)
               return -1;
            _wasm_alloc->free<char>(-pages);
         } else {
            if (!mod.memories.size() || _max_pages - sz < static_cast<uint32_t>(pages) ||
                (mod.memories[0].limits.flags && (static_cast<int32_t>(mod.memories[0].limits.maximum) - sz < pages)))
               return -1;
            _wasm_alloc->alloc<char>(pages);
         }
         return sz;
      }

      inline int32_t current_linear_memory() const { return _wasm_alloc->get_current_page(); }
      inline void    exit(std::error_code err = std::error_code()) {
         // FIXME: system_error?
         _error_code = err;
         throw wasm_exit_exception{"Exiting"};
      }

      inline void        set_module(module* mod) { _mod = mod; }
      inline module&     get_module() { return *_mod; }
      inline void        set_wasm_allocator(wasm_allocator* alloc) { _wasm_alloc = alloc; }
      inline auto        get_wasm_allocator() { return _wasm_alloc; }
      inline char*       linear_memory() { return _linear_memory; }
      inline auto&       get_operand_stack() { return _os; }
      inline const auto& get_operand_stack() const { return _os; }
      inline auto        get_interface() { return execution_interface{ _linear_memory, &_os }; }
      void               set_max_pages(std::uint32_t max_pages) { _max_pages = std::min(max_pages, static_cast<std::uint32_t>(vm::max_pages)); }

      inline std::error_code get_error_code() const { return _error_code; }

      template<typename Module>
      inline void reset(Module& mod) {
         CORE_NET_VM_ASSERT(_mod->error == nullptr, wasm_interpreter_exception, _mod->error);

         // Reset the capacity of underlying memory used by operand stack if it is
         // greater than initial_stack_size
         _os.reset_capacity();

         _linear_memory = _wasm_alloc->get_base_ptr<char>();
         if(mod.memories.size()) {
            CORE_NET_VM_ASSERT(mod.memories[0].limits.initial <= _max_pages, wasm_bad_alloc, "Cannot allocate initial linear memory.");
            _wasm_alloc->reset(mod.memories[0].limits.initial);
         } else
            _wasm_alloc->reset();

         for (uint32_t i = 0; i < mod.data.size(); i++) {
            const auto& data_seg = mod.data[i];
            uint32_t offset = data_seg.offset.value.i32; // force to unsigned
            auto available_memory =  mod.memories[0].limits.initial * static_cast<uint64_t>(page_size);
            auto required_memory = static_cast<uint64_t>(offset) + data_seg.data.size();
            CORE_NET_VM_ASSERT(required_memory <= available_memory, wasm_memory_exception, "data out of range");
            auto addr = _linear_memory + offset;
            if(data_seg.data.size())
               memcpy((char*)(addr), data_seg.data.data(), data_seg.data.size());
         }

         // Globals can be different from one WASM code to another.
         // Need to clear _globals at the start of an execution.
         _globals.clear();
         _globals.reserve(mod.globals.size());
         for (uint32_t i = 0; i < mod.globals.size(); i++) {
            _globals.emplace_back(mod.globals[i].init);
         }
      }

      template <typename Visitor, typename... Args>
      inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, const std::string_view func,
                                               Args&&... args) {
         uint32_t func_index = _mod->get_exported_function(func);
         return derived().execute(host, std::forward<Visitor>(visitor), func_index, std::forward<Args>(args)...);
      }

      template <typename Visitor, typename... Args>
      inline void execute_start(host_type* host, Visitor&& visitor) {
         if (_mod->start != std::numeric_limits<uint32_t>::max())
            derived().execute(host, std::forward<Visitor>(visitor), _mod->start);
      }

    protected:

      template<typename Func_type, typename... Args>
      static void type_check_args(const Func_type& ft, Args&&...) {
         CORE_NET_VM_ASSERT(sizeof...(Args) == ft.param_types.size(), wasm_interpreter_exception, "wrong number of arguments");
         uint32_t i = 0;
         CORE_NET_VM_ASSERT((... && (to_wasm_type_v<detail::type_converter_t<Host>, Args> == ft.param_types.at(i++))), wasm_interpreter_exception, "unexpected argument type");
      }

      static void handle_signal(int sig) {
         switch(sig) {
          case SIGSEGV:
          case SIGBUS:
          case SIGFPE:
            break;
          default:
            /* TODO fix this */
            assert(!"??????");
         }
         throw wasm_memory_exception{ "wasm memory out-of-bounds" };
      }

      char*                           _linear_memory    = nullptr;
      module*                         _mod = nullptr;
      wasm_allocator*                 _wasm_alloc;
      uint32_t                        _max_pages = max_pages;
      detail::host_invoker_t<Host>    _rhf;
      std::error_code                 _error_code;
      operand_stack                   _os;
      std::vector<init_expr>          _globals;
   };

   struct jit_visitor { template<typename T> jit_visitor(T&&) {} };

   template<typename Host>
   class null_execution_context : public execution_context_base<null_execution_context<Host>, Host, false> {
      using base_type = execution_context_base<null_execution_context<Host>, Host, false>;
   public:
      null_execution_context() {}
      null_execution_context(module& m, std::uint32_t max_call_depth) : base_type(&m) {}
   };

   template<bool EnableBacktrace>
   struct frame_info_holder {};
   template<>
   struct frame_info_holder<true> {
      void* volatile _bottom_frame = nullptr;
      void* volatile _top_frame = nullptr;
   };

   template<typename Host, bool EnableBacktrace = false>
   class jit_execution_context : public frame_info_holder<EnableBacktrace>, public execution_context_base<jit_execution_context<Host, EnableBacktrace>, Host, true> {
      using base_type = execution_context_base<jit_execution_context<Host, EnableBacktrace>, Host, true>;
      using host_type  = detail::host_type_t<Host>;
   public:
      using base_type::execute;
      using base_type::base_type;
      using base_type::_mod;
      using base_type::_rhf;
      using base_type::_error_code;
      using base_type::handle_signal;
      using base_type::get_operand_stack;
      using base_type::linear_memory;
      using base_type::get_interface;
      using base_type::_globals;

      jit_execution_context() {}

      jit_execution_context(module& m, std::uint32_t max_call_depth) : base_type(&m), _remaining_call_depth(max_call_depth) {}

      void set_max_call_depth(std::uint32_t max_call_depth) {
         _remaining_call_depth = max_call_depth;
      }

      // call_host_function is called from two sites:
      //   1. execute() in C++: args_raw is a packed native_value[] (stride 1)
      //   2. JIT host stub: stack points to JIT operand stack (stride 2 on aarch64)
      // The jit_call parameter distinguishes the two cases.
      inline native_value call_host_function(native_value* stack, uint32_t index, bool jit_call = false) {
         const auto& ft = _mod->jit_mod->get_function_type(index);
         uint32_t num_params = ft.param_types.size();
#ifndef NDEBUG
         uint32_t original_operands = get_operand_stack().size();
#endif
#ifdef __aarch64__
         // On AArch64, JIT operand stack slots are 16 bytes (value + 8-byte padding),
         // so native_value elements are spaced 2 apart. But when called from C++
         // execute(), the args array is packed at stride 1.
         const uint32_t stack_stride = jit_call ? 2 : 1;
         // (debug traces removed)
#else
         constexpr uint32_t stack_stride = 1;
         (void)jit_call;
#endif
         for(uint32_t i = 0; i < ft.param_types.size(); ++i) {
            uint32_t slot = (num_params - i - 1) * stack_stride;
            switch(ft.param_types[i]) {
             case i32: get_operand_stack().push(i32_const_t{stack[slot].i32}); break;
             case i64: get_operand_stack().push(i64_const_t{stack[slot].i64}); break;
             case f32: get_operand_stack().push(f32_const_t{stack[slot].f32}); break;
             case f64: get_operand_stack().push(f64_const_t{stack[slot].f64}); break;
             default: assert(!"Unexpected type in param_types.");
            }
         }
         _rhf(_host, get_interface(), _mod->jit_mod->import_functions[index]);
         native_value result{uint64_t{0}};
         // guarantee that the junk bits are zero, to avoid problems.
         auto set_result = [&result](auto val) { std::memcpy(&result, &val, sizeof(val)); };
         if(ft.return_count) {
            operand_stack_elem el = get_operand_stack().pop();
            switch(ft.return_type) {
             case i32: set_result(el.to_ui32()); break;
             case i64: set_result(el.to_ui64()); break;
             case f32: set_result(el.to_f32()); break;
             case f64: set_result(el.to_f64()); break;
             default: assert(!"Unexpected function return type.");
            }
         }

         assert(get_operand_stack().size() == original_operands);
         return result;
      }

      inline void reset() {
         base_type::reset(*(_mod->jit_mod));
         get_operand_stack().eat(0);
      }

      template <typename... Args>
      inline std::optional<operand_stack_elem> execute(host_type* host, jit_visitor, uint32_t func_index, Args&&... args) {
         auto saved_host = _host;
         auto saved_os_size = get_operand_stack().size();
         auto g = scope_guard([&](){ _host = saved_host; get_operand_stack().eat(saved_os_size); });

         _host = host;

         const auto& ft = _mod->jit_mod->get_function_type(func_index);
         this->type_check_args(ft, std::forward<Args>(args)... ); // args not modified by type_check_args
         native_value result;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
         // Calling execute() with no `args` (i.e. `execute(host_type,jit_visitor,uint32_t)`) results in a "statement has no
         // effect [-Werror=unused-value]" warning on this line. Dissable warning.
         native_value args_raw[] = { transform_arg( std::forward<Args>(args))... };
#pragma GCC diagnostic pop

         try {
            if (func_index < _mod->jit_mod->get_imported_functions_size()) {
               std::reverse(args_raw + 0, args_raw + sizeof...(Args));
               result = call_host_function(args_raw, func_index);
            } else {
               std::size_t maximum_stack_usage =
                  (_mod->maximum_stack + 2 /*frame ptr + return ptr*/) * (_remaining_call_depth + 1) +
                 sizeof...(Args) + 4 /* scratch space */;
               // On AArch64, each operand stack slot is 16 bytes (stp/ldp pairs for alignment)
               // vs 8 bytes on x86_64. Scale the allocation accordingly. Additionally,
               // re-entrant host calls consume C++ stack frames at each nesting level
               // (call_host_function → chain runtime → execute → trampoline). Budget
               // ~64KB per re-entrant level for these C++ frames on AArch64.
#ifdef __aarch64__
               constexpr std::size_t stack_slot_size = 16;
               constexpr std::size_t cpp_frame_budget_per_level = 64 * 1024;
               std::size_t alt_stack_size = maximum_stack_usage * stack_slot_size
                  + (_remaining_call_depth + 1) * cpp_frame_budget_per_level;
#else
               constexpr std::size_t stack_slot_size = sizeof(native_value);
               std::size_t alt_stack_size = maximum_stack_usage * stack_slot_size;
#endif
               stack_allocator alt_stack(alt_stack_size);
               // Reserve space at the top of the alternate stack for data
               // accessed by inline assembly (saved sp, saved FPCR).
               void* stack = alt_stack.top();
               if(stack) {
#ifdef __aarch64__
                  // AArch64 requires 16-byte stack alignment. Reserve 32 bytes
                  // (2 x 16-byte slots) for saved sp and FPCR, keeping alignment.
                  stack = static_cast<char*>(stack) - 32;
#else
                  stack = static_cast<char*>(stack) - 24;
#endif
               }
               auto jit_idx = func_index - _mod->jit_mod->get_imported_functions_size();
               auto offset = _mod->jit_mod->jit_code_offset[jit_idx];
               auto code_base = _mod->allocator._code_base;
               auto fn = reinterpret_cast<native_value (*)(void*, void*)>(offset + code_base);

               if constexpr(EnableBacktrace) {
                  sigset_t block_mask;
                  sigemptyset(&block_mask);
                  sigaddset(&block_mask, SIGPROF);
                  pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
                  auto restore = scope_guard{[this, &block_mask] {
                     this->_top_frame = nullptr;
                     this->_bottom_frame = nullptr;
                     pthread_sigmask(SIG_UNBLOCK, &block_mask, nullptr);
                  }};

                  vm::invoke_with_signal_handler([&]() {
                     result = execute<sizeof...(Args)>(args_raw, fn, this, base_type::linear_memory(), stack);
                  }, &handle_signal, _mod->allocator, base_type::get_wasm_allocator());
               } else {
                  vm::invoke_with_signal_handler([&]() {
                     result = execute<sizeof...(Args)>(args_raw, fn, this, base_type::linear_memory(), stack);
                  }, &handle_signal, _mod->allocator, base_type::get_wasm_allocator());
               }
            }
         } catch(wasm_exit_exception&) {
            return {};
         }

         if(!ft.return_count)
            return {};
         else switch (ft.return_type) {
            case i32: return {i32_const_t{result.i32}};
            case i64: return {i64_const_t{result.i64}};
            case f32: return {f32_const_t{result.f32}};
            case f64: return {f64_const_t{result.f64}};
            default: assert(!"Unexpected function return type");
         }
         __builtin_unreachable();
      }

#if defined(__x86_64__) || defined(__aarch64__)
      int backtrace(void** out, int count, void* uc) const {
         static_assert(EnableBacktrace);
         void* end = this->_top_frame;
         if(end == nullptr) return 0;
         void* rbp;
         int i = 0;
         if(this->_bottom_frame) {
            rbp = this->_bottom_frame;
         } else if(count != 0) {
            if(uc) {
#ifdef __x86_64__
#  ifdef __APPLE__
               auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rip);
               rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rbp);
               auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rsp);
#  elif defined __FreeBSD__
               auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rip);
               rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rbp);
               auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rsp);
#  else
               auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RIP]);
               rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RBP]);
               auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RSP]);
#  endif
               out[i++] = rip;
               // If we were interrupted in the function prologue or epilogue,
               // avoid dropping the parent frame.
               auto code_base = reinterpret_cast<const unsigned char*>(_mod->allocator.get_code_start());
               auto code_end = code_base + _mod->allocator._code_size;
               if(rip >= code_base && rip < code_end && count > 1) {
                  // function prologue: push rbp
                  if(*reinterpret_cast<const unsigned char*>(rip) == 0x55) {
                     if(rip != *static_cast<void**>(rsp)) {
                        out[i++] = *static_cast<void**>(rsp);
                     }
                  } else if(rip[0] == 0x48 && rip[1] == 0x89 && (rip[2] == 0xe5 || rip[2] == 0x27)) {
                     if((rip - 1) != static_cast<void**>(rsp)[1]) {
                        out[i++] = static_cast<void**>(rsp)[1];
                     }
                  }
                  // function epilogue: ret
                  else if(rip[0] == 0xc3) {
                     out[i++] = *static_cast<void**>(rsp);
                  }
               }
#elif defined(__aarch64__)
               // AArch64: read pc, fp (x29), lr (x30) from ucontext
               auto pc  = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext.pc);
               rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.regs[29]); // x29 = fp
               auto lr  = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.regs[30]); // x30 = lr
               out[i++] = pc;
               auto code_base = reinterpret_cast<const unsigned char*>(_mod->allocator.get_code_start());
               auto code_end = code_base + _mod->allocator._code_size;
               if(pc >= code_base && pc < code_end && count > 1) {
                  uint32_t instr;
                  memcpy(&instr, pc, 4);
                  // function prologue: stp x29, x30, [sp, ...] — encoding starts with 0xa9
                  if((instr & 0xFF000000u) == 0xa9000000u) {
                     out[i++] = lr;
                  }
                  // function epilogue: ret — encoding 0xd65f03c0
                  else if(instr == 0xd65f03c0u) {
                     out[i++] = lr;
                  }
               }
#endif // __x86_64__ / __aarch64__
            } else {
               rbp = __builtin_frame_address(0);
            }
         }
         while(i < count) {
            void* rip = static_cast<void**>(rbp)[1];
            if(rbp == end) break;
            out[i++] = rip;
            rbp = *static_cast<void**>(rbp);
         }
         return i;
      }

      static constexpr bool async_backtrace() { return EnableBacktrace; }
#endif // __x86_64__ || __aarch64__ (backtrace)

      inline int32_t get_global_i32(uint32_t index) {
         return _globals[index].value.i32;
      }

      inline int64_t get_global_i64(uint32_t index) {
         return _globals[index].value.i64;
      }

      inline uint32_t get_global_f32(uint32_t index) {
         return _globals[index].value.f32;
      }

      inline uint64_t get_global_f64(uint32_t index) {
         return _globals[index].value.f64;
      }

      inline void set_global_i32(uint32_t index, int32_t value) {
         _globals[index].value.i32 = value;
      }

      inline void set_global_i64(uint32_t index, int64_t value) {
         _globals[index].value.i64 = value;
      }

      inline void set_global_f32(uint32_t index, uint32_t value) {
          _globals[index].value.f32 = value;
      }

      inline void set_global_f64(uint32_t index, uint64_t value) {
         _globals[index].value.f64 = value;
      }

   protected:

      template<typename T>
      native_value transform_arg(T&& value) {
         // make sure that the garbage bits are always zero.
         native_value result;
         std::memset(&result, 0, sizeof(result));
         auto tc = detail::type_converter_t<Host>{_host, get_interface()};
         auto transformed_value = detail::resolve_result(tc, std::forward<T>(value)).data;
         std::memcpy(&result, &transformed_value, sizeof(transformed_value));
         return result;
      }

#if defined(__x86_64__) || defined(__aarch64__)
      /* TODO abstract this and clean this up a bit, this really doesn't belong here */
      template<int Count>
      static native_value execute(native_value* data, native_value (*fun)(void*, void*), jit_execution_context* context, void* linear_memory, void* stack) {
         static_assert(sizeof(native_value) == 8, "8-bytes expected for native_value");
         native_value result;
         unsigned stack_check = context->_remaining_call_depth;
#ifdef __x86_64__
         // TODO refactor this whole thing to not need all of this, should be generated from the backend
         // currently ignoring register c++17 warning
         register void* stack_top asm ("r12") = stack;
         // 0x1f80 is the default MXCSR value
#define ASM_CODE(before, after)                                         \
         asm volatile(                                                  \
            "test %[stack_top], %[stack_top]; "                          \
            "jnz 3f; "                                                   \
            "mov %%rsp, %[stack_top]; "                                  \
            "sub $0x98, %%rsp; " /* red-zone + 24 bytes*/                \
            "mov %[stack_top], (%%rsp); "                                \
            "jmp 4f; "                                                   \
            "3: "                                                        \
            "mov %%rsp, (%[stack_top]); "                                \
            "mov %[stack_top], %%rsp; "                                  \
            "4: "                                                        \
            "stmxcsr 16(%%rsp); "                                        \
            "mov $0x1f80, %%rax; "                                       \
            "mov %%rax, 8(%%rsp); "                                      \
            "ldmxcsr 8(%%rsp); "                                         \
            "mov %[Count], %%rax; "                                      \
            "test %%rax, %%rax; "                                        \
            "jz 2f; "                                                    \
            "1: "                                                        \
            "movq (%[data]), %%r8; "                                     \
            "lea 8(%[data]), %[data]; "                                  \
            "pushq %%r8; "                                               \
            "dec %%rax; "                                                \
            "jnz 1b; "                                                   \
            "2: "                                                        \
            before                                                       \
            "callq *%[fun]; "                                            \
            after                                                        \
            "add %[StackOffset], %%rsp; "                                \
            "ldmxcsr 16(%%rsp); "                                        \
            "mov (%%rsp), %%rsp; "                                       \
            /* Force explicit register allocation, because otherwise it's too hard to get the clobbers right. */ \
            : [result] "=&a" (result), /* output, reused as a scratch register */ \
              [data] "+d" (data), [fun] "+c" (fun), [stack_top] "+r" (stack_top) /* input only, but may be clobbered */ \
            : [context] "D" (context), [linear_memory] "S" (linear_memory), \
              [StackOffset] "n" (Count*8), [Count] "n" (Count), "b" (stack_check) /* input */ \
            : "memory", "cc", /* clobber */                              \
              /* call clobbered registers, that are not otherwise used */  \
              /*"rax", "rcx", "rdx", "rsi", "rdi",*/ "r8", "r9", "r10", "r11", \
              "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", \
              "mm0","mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",       \
              "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" \
         );
         if constexpr (!EnableBacktrace) {
            ASM_CODE("", "");
         } else {
            ASM_CODE("movq %%rbp, 8(%[context]); ",
                     "xor %[fun], %[fun]; "
                     "mov %[fun], 8(%[context]); ");
         }
#undef ASM_CODE
#elif defined(__aarch64__)
         // AArch64 JIT trampoline
         // Register convention:
         //   x0 = context, x1 = linear_memory (set by trampoline before blr)
         //   x19 = call depth counter (callee-saved)
         //   x20 = alt stack pointer (callee-saved)
         //   x29 = frame pointer, x30 = link register
         // Pin callee-saved registers used explicitly by the asm block.
         // The compiler must not assign other operands to these registers.
         // x19 = call depth counter (loaded by asm from stack_check_reg)
         // x20 = alt stack pointer
         // x21 = context backup
         // x22 = linear_memory backup
         // x23 = stack_check value (source for mov x19, ...)
         register unsigned call_depth_pin asm ("x19") = stack_check; // reserve x19
         register void* stack_top asm ("x20") = stack;
         register void* ctx_reg asm ("x21") = context;
         register void* mem_reg asm ("x22") = linear_memory;
         register unsigned stack_check_reg asm ("x23") = stack_check;
#define ASM_CODE_A64(before, after)                                     \
         asm volatile(                                                  \
            /* Save callee-saved registers */                            \
            "stp x29, x30, [sp, #-96]!\n"                               \
            "stp x19, x20, [sp, #16]\n"                                 \
            "stp x21, x22, [sp, #32]\n"                                 \
            "stp x23, x24, [sp, #48]\n"                                 \
            "stp x25, x26, [sp, #64]\n"                                 \
            "stp x27, x28, [sp, #80]\n"                                 \
            "mov x29, sp\n"                                              \
            /* Switch stack if alternate stack provided */                \
            "cbz %[stack_top], 3f\n"                                     \
            "mov x9, sp\n"                                               \
            "str x9, [%[stack_top]]\n"                                   \
            "mov sp, %[stack_top]\n"                                     \
            "b 4f\n"                                                     \
            "3:\n"                                                       \
            "mov %[stack_top], sp\n"                                     \
            "sub sp, sp, #0x60\n"   /* reserve space (no red zone on aarch64) */ \
            "str %[stack_top], [sp]\n"                                   \
            "4:\n"                                                       \
            /* Save and reset FPCR */                                    \
            "mrs x9, fpcr\n"                                             \
            "str x9, [sp, #16]\n"                                        \
            "mov x9, #0\n"       /* default FPCR: round-to-nearest */    \
            "msr fpcr, x9\n"                                             \
            /* Push WASM arguments (pairs for 16-byte alignment) */      \
            "mov x9, %[Count]\n"                                         \
            "cbz x9, 2f\n"                                               \
            "1:\n"                                                       \
            "ldr x10, [%[data]], #8\n"                                   \
            "stp x10, xzr, [sp, #-16]!\n"                               \
            "subs x9, x9, #1\n"                                          \
            "b.ne 1b\n"                                                  \
            "2:\n"                                                       \
            /* Set up registers for JIT code:                         */ \
            /*   x0 = context, x1 = linear_memory, x19 = call depth  */ \
            "mov x19, %[stack_check]\n"                                  \
            "mov x0, %[ctx_reg]\n"                                       \
            "mov x1, %[mem_reg]\n"                                       \
            before                                                       \
            "blr %[fun]\n"                                               \
            /* Save result in x9 (scratch) — x0 will be clobbered */    \
            "mov x9, x0\n"                                              \
            after                                                        \
            /* Clean up argument stack */                                 \
            "add sp, sp, %[StackOffset]\n"                               \
            /* Restore FPCR and stack */                                 \
            "ldr x10, [sp, #16]\n"                                       \
            "msr fpcr, x10\n"                                            \
            "ldr x10, [sp]\n"                                            \
            "mov sp, x10\n"                                              \
            /* Restore callee-saved registers */                         \
            "ldp x27, x28, [sp, #80]\n"                                  \
            "ldp x25, x26, [sp, #64]\n"                                  \
            "ldp x23, x24, [sp, #48]\n"                                  \
            "ldp x21, x22, [sp, #32]\n"                                  \
            "ldp x19, x20, [sp, #16]\n"                                  \
            "ldp x29, x30, [sp], #96\n"                                  \
            /* NOW move result to output — after all restores */         \
            "mov %[result], x9\n"                                        \
            : [result] "=&r" (result),                                   \
              [data] "+r" (data), [fun] "+r" (fun), [stack_top] "+r" (stack_top) \
            : [ctx_reg] "r" (ctx_reg), [mem_reg] "r" (mem_reg),          \
              [StackOffset] "n" (Count*16), [Count] "n" (Count),         \
              [stack_check] "r" (stack_check_reg),                       \
              [_x19_reserve] "r" (call_depth_pin) /* pin x19 */          \
            : "memory", "cc",                                            \
              "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8",   \
              "x9", "x10", "x11", "x12", "x13", "x14", "x15",          \
              "x16", "x17", "x18",                                       \
              "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",           \
              "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",     \
              "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",   \
              "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"    \
         );
         if constexpr (!EnableBacktrace) {
            ASM_CODE_A64("", "");
         } else {
            ASM_CODE_A64(
               "str x29, [x0, #8]\n",       /* x0 = context, set above */
               "str xzr, [%[ctx_reg], #8]\n" /* x0 may be clobbered by JIT */
            );
         }
#undef ASM_CODE_A64
#endif // __x86_64__ / __aarch64__
         return result;
      }
#endif // defined(__x86_64__) || defined(__aarch64__)

      host_type * _host = nullptr;
      uint32_t _remaining_call_depth = 0;
   };

   template <typename Host>
   class execution_context : public execution_context_base<execution_context<Host>, Host, false> {
      using base_type = execution_context_base<execution_context<Host>, Host, false>;
      using host_type  = detail::host_type_t<Host>;
    public:
      using base_type::_mod;
      using base_type::_rhf;
      using base_type::_error_code;
      using base_type::handle_signal;
      using base_type::get_operand_stack;
      using base_type::linear_memory;
      using base_type::get_interface;
      using base_type::_globals;

      execution_context()
       : base_type(), _halt(exit_t{}) {}

      execution_context(module& m, uint32_t max_call_depth)
       : base_type(&m), _base_allocator{max_call_depth*sizeof(activation_frame)},
         _as{max_call_depth, _base_allocator}, _halt(exit_t{}) {}

      void set_max_call_depth(uint32_t max_call_depth) {
         static_assert(std::is_trivially_move_assignable_v<call_stack>, "This is seriously broken if call_stack move assignment might use the existing memory");
         std::size_t mem_size = max_call_depth*sizeof(activation_frame);
         if(mem_size > _base_allocator.mem_size) {
            _base_allocator = bounded_allocator{mem_size};
            _as = call_stack{max_call_depth, _base_allocator};
         } else if (max_call_depth != _as.capacity()){
            _base_allocator.index = 0;
            _as = call_stack{max_call_depth, _base_allocator};
         }
      }

      inline void call(uint32_t index) {
         // TODO validate index is valid
         if (index < _mod->get_imported_functions_size()) {
            // TODO validate only importing functions
            const auto& ft = _mod->types[_mod->imports[index].type.func_t];
            type_check(ft);
            inc_pc();
            push_call( activation_frame{ nullptr, 0 } );
            _rhf(_state.host, get_interface(), _mod->import_functions[index]);
            pop_call();
         } else {
            // const auto& ft = _mod->types[_mod->functions[index - _mod->get_imported_functions_size()]];
            // type_check(ft);
            push_call(index);
            setup_locals(index);
            set_pc( _mod->get_function_pc(index) );
         }
      }

      void print_stack() {
         std::cout << "STACK { ";
         for (int i = 0; i < get_operand_stack().size(); i++) {
            std::cout << "(" << i << ")";
            visit(overloaded { [&](i32_const_t el) { std::cout << "i32:" << el.data.ui << ", "; },
                               [&](i64_const_t el) { std::cout << "i64:" << el.data.ui << ", "; },
                               [&](f32_const_t el) { std::cout << "f32:" << el.data.f << ", "; },
                               [&](f64_const_t el) { std::cout << "f64:" << el.data.f << ", "; },
                               [&](auto el) { std::cout << "(INDEX " << el.index() << "), "; } }, get_operand_stack().get(i));
         }
         std::cout << " }\n";
      }

      inline uint32_t       table_elem(uint32_t i) { return _mod->tables[0].table[i]; }
      inline void           push_operand(operand_stack_elem el) { get_operand_stack().push(std::move(el)); }
      inline operand_stack_elem get_operand(uint32_t index) const { return get_operand_stack().get(_last_op_index + index); }
      inline void           eat_operands(uint32_t index) { get_operand_stack().eat(index); }
      inline void           compact_operand(uint32_t index) { get_operand_stack().compact(index); }
      inline void           set_operand(uint32_t index, const operand_stack_elem& el) { get_operand_stack().set(_last_op_index + index, el); }
      inline uint32_t       current_operands_index() const { return get_operand_stack().current_index(); }
      inline void           push_call(activation_frame&& el) { _as.push(std::move(el)); }
      inline activation_frame pop_call() { return _as.pop(); }
      inline uint32_t       call_depth()const { return _as.size(); }
      template <bool Should_Exit=false>
      inline void           push_call(uint32_t index) {
         opcode* return_pc = static_cast<opcode*>(&_halt);
         if constexpr (!Should_Exit)
            return_pc = _state.pc + 1;

         _as.push( activation_frame{ return_pc, _last_op_index } );
         _last_op_index = get_operand_stack().size() - _mod->get_function_type(index).param_types.size();
      }

      inline void apply_pop_call(uint32_t num_locals, uint16_t return_count) {
         const auto& af = _as.pop();
         _state.pc = af.pc;
         _last_op_index = af.last_op_index;
         if (return_count)
            compact_operand(get_operand_stack().size() - num_locals - 1);
         else
            eat_operands(get_operand_stack().size() - num_locals);
      }
      inline operand_stack_elem  pop_operand() { return get_operand_stack().pop(); }
      inline operand_stack_elem& peek_operand(size_t i = 0) { return get_operand_stack().peek(i); }
      inline operand_stack_elem  get_global(uint32_t index) {
         CORE_NET_VM_ASSERT(index < _mod->globals.size(), wasm_interpreter_exception, "global index out of range");
         CORE_NET_VM_ASSERT(index < _globals.size(), wasm_interpreter_exception, "index for _globals out of range in get_global for interpreter");
         const auto& gl = _mod->globals[index];
         switch (gl.type.content_type) {
            case types::i32: return i32_const_t{ _globals[index].value.i32 };
            case types::i64: return i64_const_t{ _globals[index].value.i64 };
            case types::f32: return f32_const_t{ _globals[index].value.f32 };
            case types::f64: return f64_const_t{ _globals[index].value.f64 };
            default: throw wasm_interpreter_exception{ "invalid global type" };
         }
      }

      inline void set_global(uint32_t index, const operand_stack_elem& el) {
         CORE_NET_VM_ASSERT(index < _mod->globals.size(), wasm_interpreter_exception, "global index out of range");
         CORE_NET_VM_ASSERT(index < _globals.size(), wasm_interpreter_exception, "index for _globals out of range");
         auto& gl = _mod->globals[index];
         CORE_NET_VM_ASSERT(gl.type.mutability, wasm_interpreter_exception, "global is not mutable");
         visit(overloaded{ [&](const i32_const_t& i) {
                                  CORE_NET_VM_ASSERT(gl.type.content_type == types::i32, wasm_interpreter_exception,
                                                "expected i32 global type");
                                  _globals[index].value.i32 = i.data.ui;
                               },
                                [&](const i64_const_t& i) {
                                   CORE_NET_VM_ASSERT(gl.type.content_type == types::i64, wasm_interpreter_exception,
                                                 "expected i64 global type");
                                   _globals[index].value.i64 = i.data.ui;
                                },
                                [&](const f32_const_t& f) {
                                   CORE_NET_VM_ASSERT(gl.type.content_type == types::f32, wasm_interpreter_exception,
                                                 "expected f32 global type");
                                   _globals[index].value.f32 = f.data.ui;
                                },
                                [&](const f64_const_t& f) {
                                   CORE_NET_VM_ASSERT(gl.type.content_type == types::f64, wasm_interpreter_exception,
                                                 "expected f64 global type");
                                   _globals[index].value.f64 = f.data.ui;
                                },
                                [](auto) { throw wasm_interpreter_exception{ "invalid global type" }; } },
                    el);
      }

      inline bool is_true(const operand_stack_elem& el) {
         bool ret_val = false;
         visit(overloaded{ [&](const i32_const_t& i32) { ret_val = i32.data.ui; },
                           [&](auto) { throw wasm_invalid_element{ "should be an i32 type" }; } },
                    el);
         return ret_val;
      }

      inline void type_check(const func_type& ft) {
         for (uint32_t i = 0; i < ft.param_types.size(); i++) {
            const auto& op = peek_operand((ft.param_types.size() - 1) - i);
            visit(overloaded{ [&](const i32_const_t&) {
                                     CORE_NET_VM_ASSERT(ft.param_types[i] == types::i32, wasm_interpreter_exception,
                                                   "function param type mismatch");
                                  },
                                   [&](const f32_const_t&) {
                                      CORE_NET_VM_ASSERT(ft.param_types[i] == types::f32, wasm_interpreter_exception,
                                                    "function param type mismatch");
                                   },
                                   [&](const i64_const_t&) {
                                      CORE_NET_VM_ASSERT(ft.param_types[i] == types::i64, wasm_interpreter_exception,
                                                    "function param type mismatch");
                                   },
                                   [&](const f64_const_t&) {
                                      CORE_NET_VM_ASSERT(ft.param_types[i] == types::f64, wasm_interpreter_exception,
                                                    "function param type mismatch");
                                   },
                                   [&](auto) { throw wasm_interpreter_exception{ "function param invalid type" }; } },
                       op);
         }
      }

      inline opcode*  get_pc() const { return _state.pc; }
      inline void     set_relative_pc(uint32_t pc_offset) {
         _state.pc = _mod->code[0].code + pc_offset;
      }
      inline void     set_pc(opcode* pc) { _state.pc = pc; }
      inline void     inc_pc(uint32_t offset=1) { _state.pc += offset; }
      inline void     exit(std::error_code err = std::error_code()) {
         _error_code = err;
         _state.pc = &_halt;
         _state.exiting = true;
      }

      inline void reset() {
         base_type::reset(*_mod);
         _state = execution_state{};
         get_operand_stack().eat(_state.os_index);
         _as.eat(_state.as_index);
      }

      template <typename Visitor, typename... Args>
      inline std::optional<operand_stack_elem> execute_func_table(host_type* host, Visitor&& visitor, uint32_t table_index,
                                                                  Args&&... args) {
         return execute(host, std::forward<Visitor>(visitor), table_elem(table_index), std::forward<Args>(args)...);
      }

      template <typename Visitor, typename... Args>
      inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, const std::string_view func,
                                                       Args&&... args) {
         uint32_t func_index = _mod->get_exported_function(func);
         return execute(host, std::forward<Visitor>(visitor), func_index, std::forward<Args>(args)...);
      }

      template <typename Visitor, typename... Args>
      inline void execute_start(host_type* host, Visitor&& visitor) {
         if (_mod->start != std::numeric_limits<uint32_t>::max())
            execute(host, std::forward<Visitor>(visitor), _mod->start);
      }

      template <typename Visitor, typename... Args>
      inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, uint32_t func_index, Args&&... args) {
         CORE_NET_VM_ASSERT(func_index < std::numeric_limits<uint32_t>::max(), wasm_interpreter_exception,
                       "cannot execute function, function not found");

         auto last_last_op_index = _last_op_index;

         // save the state of the original calling context
         execution_state saved_state = _state;

         _state.host             = host;
         _state.as_index         = _as.size();
         _state.os_index         = get_operand_stack().size();

         auto cleanup = scope_guard([&]() {
            get_operand_stack().eat(_state.os_index);
            _as.eat(_state.as_index);
            _state = saved_state;

            _last_op_index = last_last_op_index;
         });

         this->type_check_args(_mod->get_function_type(func_index), std::forward<Args>(args)...); // args not modified
         push_args(std::forward<Args>(args)...);
         push_call<true>(func_index);

         if (func_index < _mod->get_imported_functions_size()) {
            _rhf(_state.host, get_interface(), _mod->import_functions[func_index]);
         } else {
            _state.pc = _mod->get_function_pc(func_index);
            setup_locals(func_index);
            vm::invoke_with_signal_handler([&]() {
               execute(std::forward<Visitor>(visitor));
            }, &handle_signal, _mod->allocator, base_type::get_wasm_allocator());
         }

         if (_mod->get_function_type(func_index).return_count && !_state.exiting) {
            return pop_operand();
         } else {
            return {};
         }
      }

      inline void jump(uint32_t pop_info, uint32_t new_pc) {
         set_relative_pc(new_pc);
         if ((pop_info & 0x80000000u)) {
            const auto& op = pop_operand();
            eat_operands(get_operand_stack().size() - ((pop_info & 0x7FFFFFFFu) - 1));
            push_operand(op);
         } else {
            eat_operands(get_operand_stack().size() - pop_info);
         }
      }

      // This isn't async-signal-safe.  Cross fingers and hope for the best.
      // It's only used for profiling.
      int backtrace(void** data, int limit, void* uc) const {
         int out = 0;
         if(limit != 0) {
            data[out++] = _state.pc;
         }
         for(int i = 0; out < limit && i < _as.size(); ++i) {
            data[out++] = _as.get_back(i).pc;
         }
         return out;
      }

    private:

      template <typename... Args>
      void push_args(Args&&... args) {
         auto tc = detail::type_converter_t<Host>{ _host, get_interface() };
         (void)tc;
         (... , push_operand(detail::resolve_result(tc, std::forward<Args>(args))));
      }

      inline void setup_locals(uint32_t index) {
         const auto& fn = _mod->code[index - _mod->get_imported_functions_size()];
         for (uint32_t i = 0; i < fn.locals.size(); i++) {
            for (uint32_t j = 0; j < fn.locals[i].count; j++)
               switch (fn.locals[i].type) {
                  case types::i32: push_operand(i32_const_t{ (uint32_t)0 }); break;
                  case types::i64: push_operand(i64_const_t{ (uint64_t)0 }); break;
                  case types::f32: push_operand(f32_const_t{ (uint32_t)0 }); break;
                  case types::f64: push_operand(f64_const_t{ (uint64_t)0 }); break;
                  default: throw wasm_interpreter_exception{ "invalid function param type" };
               }
         }
      }

#define CREATE_TABLE_ENTRY(NAME, CODE) &&ev_label_##NAME,
#define CREATE_LABEL(NAME, CODE)                                                                                  \
      ev_label_##NAME : std::forward<Visitor>(visitor)(ev_variant->template get<core_net::vm::CORE_NET_VM_OPCODE_T(NAME)>()); \
      ev_variant = _state.pc; \
      goto* dispatch_table[ev_variant->index()];
#define CREATE_EXIT_LABEL(NAME, CODE) ev_label_##NAME : \
      return;
#define CREATE_EMPTY_LABEL(NAME, CODE) ev_label_##NAME :  \
      throw wasm_interpreter_exception{"empty operand"};

      template <typename Visitor>
      void execute(Visitor&& visitor) {
         static void* dispatch_table[] = {
            CORE_NET_VM_CONTROL_FLOW_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_BR_TABLE_OP(CREATE_TABLE_ENTRY)
            CORE_NET_VM_RETURN_OP(CREATE_TABLE_ENTRY)
            CORE_NET_VM_CALL_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_CALL_IMM_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_PARAMETRIC_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_VARIABLE_ACCESS_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_MEMORY_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_I32_CONSTANT_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_I64_CONSTANT_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_F32_CONSTANT_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_F64_CONSTANT_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_COMPARISON_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_NUMERIC_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_CONVERSION_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_EXIT_OP(CREATE_TABLE_ENTRY)
            CORE_NET_VM_EMPTY_OPS(CREATE_TABLE_ENTRY)
            CORE_NET_VM_ERROR_OPS(CREATE_TABLE_ENTRY)
            &&__ev_last
         };
         auto* ev_variant = _state.pc;
         goto *dispatch_table[ev_variant->index()];
         while (1) {
             CORE_NET_VM_CONTROL_FLOW_OPS(CREATE_LABEL);
             CORE_NET_VM_BR_TABLE_OP(CREATE_LABEL);
             CORE_NET_VM_RETURN_OP(CREATE_LABEL);
             CORE_NET_VM_CALL_OPS(CREATE_LABEL);
             CORE_NET_VM_CALL_IMM_OPS(CREATE_LABEL);
             CORE_NET_VM_PARAMETRIC_OPS(CREATE_LABEL);
             CORE_NET_VM_VARIABLE_ACCESS_OPS(CREATE_LABEL);
             CORE_NET_VM_MEMORY_OPS(CREATE_LABEL);
             CORE_NET_VM_I32_CONSTANT_OPS(CREATE_LABEL);
             CORE_NET_VM_I64_CONSTANT_OPS(CREATE_LABEL);
             CORE_NET_VM_F32_CONSTANT_OPS(CREATE_LABEL);
             CORE_NET_VM_F64_CONSTANT_OPS(CREATE_LABEL);
             CORE_NET_VM_COMPARISON_OPS(CREATE_LABEL);
             CORE_NET_VM_NUMERIC_OPS(CREATE_LABEL);
             CORE_NET_VM_CONVERSION_OPS(CREATE_LABEL);
             CORE_NET_VM_EXIT_OP(CREATE_EXIT_LABEL);
             CORE_NET_VM_EMPTY_OPS(CREATE_EMPTY_LABEL);
             CORE_NET_VM_ERROR_OPS(CREATE_LABEL);
             __ev_last:
                throw wasm_interpreter_exception{"should never reach here"};
         }
      }

#undef CREATE_EMPTY_LABEL
#undef CREATE_LABEL
#undef CREATE_TABLE_ENTRY

      struct execution_state {
         host_type* host           = nullptr;
         uint32_t as_index         = 0;
         uint32_t os_index         = 0;
         opcode*  pc               = nullptr;
         bool     exiting          = false;
      };

      bounded_allocator _base_allocator = {
         (constants::max_call_depth + 1) * sizeof(activation_frame)
      };
      execution_state _state;
      uint32_t                        _last_op_index    = 0;
      call_stack                      _as = { _base_allocator };
      opcode                          _halt;
      host_type*                      _host = nullptr;
   };
}} // namespace core_net::vm
