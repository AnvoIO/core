#pragma once

#include <core_net/chain/wasm_interface.hpp>
#include <core_net/chain/wasm_constraints.hpp>
#include <core_net/vm/host_function.hpp>
#include <core_net/vm/argument_proxy.hpp>
#include <core_net/vm/span.hpp>
#include <core_net/vm/types.hpp>

namespace core_net { namespace chain { namespace webassembly {
   // forward declaration
   class interface;
}}} // ns core_net::chain::webassembly

// TODO need to fix up wasm_tests to not use this macro
#define CORE_NET_INJECTED_MODULE_NAME "core_net_injection"

namespace core_net { namespace chain {

   class apply_context;
   class transaction_context;

   inline static constexpr auto core_net_injected_module_name = CORE_NET_INJECTED_MODULE_NAME;

   template <typename T, std::size_t Extent = core_net::vm::dynamic_extent>
   using span = core_net::vm::span<T, Extent>;

   template <typename T, std::size_t Align = alignof(T)>
   using legacy_ptr = core_net::vm::argument_proxy<T*, Align>;

   template <typename T, std::size_t Align = alignof(T)>
   using legacy_span = core_net::vm::argument_proxy<core_net::vm::span<T>, Align>;

   struct null_terminated_ptr {
      null_terminated_ptr(const char* ptr) : ptr(ptr) {}
      const char* data() const { return ptr; }
      const char* ptr;
   };

   struct memcpy_params {
      void* dst;
      const void* src;
      vm::wasm_size_t size;
   };

   struct memcmp_params {
      const void* lhs;
      const void* rhs;
      vm::wasm_size_t size;
   };

   struct memset_params {
      const void* dst;
      const int32_t val;
      vm::wasm_size_t size;
   };

   // define the type converter for eosio
   template<typename Interface>
   struct basic_type_converter : public core_net::vm::type_converter<webassembly::interface, Interface> {
      using base_type = core_net::vm::type_converter<webassembly::interface, Interface>;
      using core_net::vm::type_converter<webassembly::interface, Interface>::type_converter;
      using base_type::from_wasm;
      using base_type::to_wasm;

      CORE_NET_VM_FROM_WASM(bool, (uint32_t value)) { return value ? 1 : 0; }

      CORE_NET_VM_FROM_WASM(memcpy_params, (vm::wasm_ptr_t dst, vm::wasm_ptr_t src, vm::wasm_size_t size)) {
         auto d = this->template validate_pointer<char>(dst, size);
         auto s = this->template validate_pointer<char>(src, size);
         this->template validate_pointer<char>(dst, 1);
         return { d, s, size };
      }

      CORE_NET_VM_FROM_WASM(memcmp_params, (vm::wasm_ptr_t lhs, vm::wasm_ptr_t rhs, vm::wasm_size_t size)) {
         auto l = this->template validate_pointer<char>(lhs, size);
         auto r = this->template validate_pointer<char>(rhs, size);
         return { l, r, size };
      }

      CORE_NET_VM_FROM_WASM(memset_params, (vm::wasm_ptr_t dst, int32_t val, vm::wasm_size_t size)) {
         auto d = this->template validate_pointer<char>(dst, size);
         this->template validate_pointer<char>(dst, 1);
         return { d, val, size };
      }

      template <typename T>
      auto from_wasm(vm::wasm_ptr_t ptr) const
         -> std::enable_if_t< std::is_pointer_v<T>,
                              vm::argument_proxy<T> > {
         auto p = this->template validate_pointer<std::remove_pointer_t<T>>(ptr, 1);
         return {p};
      }

      template <typename T>
      auto from_wasm(vm::wasm_ptr_t ptr, vm::tag<T> = {}) const
         -> std::enable_if_t< vm::is_argument_proxy_type_v<T> &&
                              std::is_pointer_v<typename T::proxy_type>, T> {
         if constexpr(T::is_legacy()) {
            EOS_ASSERT(ptr != 0, wasm_execution_error, "references cannot be created for null pointers");
         }
         auto p = this->template validate_pointer<typename T::pointee_type>(ptr, 1);
         return {p};
      }

      CORE_NET_VM_FROM_WASM(null_terminated_ptr, (vm::wasm_ptr_t ptr)) {
         auto p = this->validate_null_terminated_pointer(ptr);
         return {static_cast<const char*>(p)};
      }
      CORE_NET_VM_FROM_WASM(name, (uint64_t e)) { return name{e}; }
      uint64_t to_wasm(name&& n) { return n.to_uint64_t(); }
      CORE_NET_VM_FROM_WASM(float32_t, (float f)) { return ::to_softfloat32(f); }
      CORE_NET_VM_FROM_WASM(float64_t, (double f)) { return ::to_softfloat64(f); }
   };

   using type_converter = basic_type_converter<core_net::vm::execution_interface>;

   using eos_vm_host_functions_t = core_net::vm::registered_host_functions<webassembly::interface,
                                                                        core_net::vm::execution_interface,
                                                                        core_net::chain::type_converter>;
   using wasm_size_t = core_net::vm::wasm_size_t;

}} // core_net::chain
