#pragma once

#include <core_net/chain/webassembly/common.hpp>
#include <core_net/chain/webassembly/runtime_interface.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/apply_context.hpp>
#include <core_net/chain/wasm_config.hpp>
#include <core_net/chain/whitelisted_intrinsics.hpp>
#include <softfloat_types.h>

//eos-vm includes
#include <core_net/vm/backend.hpp>
#include <core_net/vm/profile.hpp>

namespace core_net { namespace chain { namespace webassembly { namespace eos_vm_runtime {

struct apply_options;

}}

template <typename Impl>
using eos_vm_backend_t = core_net::vm::backend<eos_vm_host_functions_t, Impl, webassembly::eos_vm_runtime::apply_options, vm::profile_instr_map>;

template <typename Options>
using eos_vm_null_backend_t = core_net::vm::backend<eos_vm_host_functions_t, core_net::vm::null_backend, Options>;

namespace webassembly { namespace eos_vm_runtime {

using namespace fc;
using namespace core_net::vm;

void validate(const bytes& code, const whitelisted_intrinsics_type& intrinsics );

void validate(const bytes& code, const wasm_config& cfg, const whitelisted_intrinsics_type& intrinsics );

struct apply_options;

struct profile_config {
   boost::container::flat_set<name> accounts_to_profile;
};

template<typename Backend>
class eos_vm_runtime : public core_net::chain::wasm_runtime_interface {
   using context_t = typename Backend::template context<eos_vm_host_functions_t>;
   public:
      eos_vm_runtime();
      std::unique_ptr<wasm_instantiated_module_interface> instantiate_module(const char* code_bytes, size_t code_size,
                                                                             const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) override;

   private:
      // todo: managing this will get more complicated with sync calls;
      // Each thread uses its own backend and exec context.
      // Their constructors do not take any arguments; therefore their life time
      // do not rely on others. Safe to be thread_local.
      thread_local static eos_vm_backend_t<Backend> _bkend;
      thread_local static context_t                 _exec_ctx;

   template<typename Impl>
   friend class eos_vm_instantiated_module;
};

class eos_vm_profile_runtime : public core_net::chain::wasm_runtime_interface {
   public:
      eos_vm_profile_runtime();
      std::unique_ptr<wasm_instantiated_module_interface> instantiate_module(const char* code_bytes, size_t code_size,
                                                                             const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) override;
};

}}}}// core_net::chain::webassembly::eos_vm_runtime
