#pragma once

#include <core_net/chain/webassembly/common.hpp>
#include <core_net/chain/webassembly/runtime_interface.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/apply_context.hpp>
#include <core_net/chain/wasm_config.hpp>
#include <core_net/chain/whitelisted_intrinsics.hpp>
#include <softfloat_types.h>

//core-vm includes
#include <core_net/vm/backend.hpp>
#include <core_net/vm/profile.hpp>

namespace core_net { namespace chain { namespace webassembly { namespace vm_runtime {

struct apply_options;

}}

template <typename Impl>
using vm_backend_t = core_net::vm::backend<vm_host_functions_t, Impl, webassembly::vm_runtime::apply_options, vm::profile_instr_map>;

template <typename Options>
using vm_null_backend_t = core_net::vm::backend<vm_host_functions_t, core_net::vm::null_backend, Options>;

// legacy aliases
template <typename Impl>
using core_vm_backend_t = vm_backend_t<Impl>;
template <typename Options>
using core_vm_null_backend_t = vm_null_backend_t<Options>;
template <typename Impl>
using eos_vm_backend_t = vm_backend_t<Impl>;
template <typename Options>
using eos_vm_null_backend_t = vm_null_backend_t<Options>;

namespace webassembly { namespace vm_runtime {

using namespace fc;
using namespace core_net::vm;

void validate(const bytes& code, const whitelisted_intrinsics_type& intrinsics );

void validate(const bytes& code, const wasm_config& cfg, const whitelisted_intrinsics_type& intrinsics );

struct apply_options;

struct profile_config {
   boost::container::flat_set<name> accounts_to_profile;
};

template<typename Backend>
class vm_runtime_impl : public core_net::chain::wasm_runtime_interface {
   using context_t = typename Backend::template context<vm_host_functions_t>;
   public:
      vm_runtime_impl();
      std::unique_ptr<wasm_instantiated_module_interface> instantiate_module(const char* code_bytes, size_t code_size,
                                                                             const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) override;

   private:
      // todo: managing this will get more complicated with sync calls;
      // Each thread uses its own backend and exec context.
      // Their constructors do not take any arguments; therefore their life time
      // do not rely on others. Safe to be thread_local.
      thread_local static vm_backend_t<Backend> _bkend;
      thread_local static context_t             _exec_ctx;

   template<typename Impl>
   friend class vm_instantiated_module;
};

class vm_profile_runtime : public core_net::chain::wasm_runtime_interface {
   public:
      vm_profile_runtime();
      std::unique_ptr<wasm_instantiated_module_interface> instantiate_module(const char* code_bytes, size_t code_size,
                                                                             const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) override;
};

// legacy aliases
template<typename Backend>
using core_vm_runtime = vm_runtime_impl<Backend>;
template<typename Backend>
using eos_vm_runtime = vm_runtime_impl<Backend>;
using core_vm_profile_runtime = vm_profile_runtime;
using eos_vm_profile_runtime = vm_profile_runtime;

}}}}// core_net::chain::webassembly::vm_runtime
