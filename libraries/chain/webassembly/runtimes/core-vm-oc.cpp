#include <core_net/chain/webassembly/core-vm-oc.hpp>
#include <core_net/chain/wasm_constraints.hpp>
#include <core_net/chain/wasm_injection.hpp>
#include <core_net/chain/apply_context.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/global_property_object.hpp>

#include <vector>
#include <iterator>

namespace core_net { namespace chain { namespace webassembly { namespace corevmoc {

class corevmoc_instantiated_module : public wasm_instantiated_module_interface {
   public:
      corevmoc_instantiated_module(const digest_type& code_hash, const uint8_t& vm_version, corevmoc_runtime& wr) :
         _code_hash(code_hash),
         _vm_version(vm_version),
         _corevmoc_runtime(wr),
         _main_thread_id(std::this_thread::get_id())
      {

      }

      ~corevmoc_instantiated_module() {
         _corevmoc_runtime.cc.free_code(_code_hash, _vm_version);
      }

      bool is_main_thread() { return _main_thread_id == std::this_thread::get_id(); };

      void apply(apply_context& context) override {
         core_net::chain::corevmoc::code_cache_sync::mode m;
         m.whitelisted = context.is_core_vm_oc_whitelisted();
         m.write_window = context.control.is_write_window();
         const code_descriptor* const cd = _corevmoc_runtime.cc.get_descriptor_for_code_sync(m, context.get_receiver(), _code_hash, _vm_version);
         CORE_ASSERT(cd, wasm_execution_error, "Core VM OC instantiation failed");

         if ( is_main_thread() )
            _corevmoc_runtime.exec.execute(*cd, _corevmoc_runtime.mem, context);
         else
            _corevmoc_runtime.exec_thread_local->execute(*cd, *_corevmoc_runtime.mem_thread_local, context);
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      corevmoc_runtime&               _corevmoc_runtime;
      std::thread::id                _main_thread_id;
};

corevmoc_runtime::corevmoc_runtime(const std::filesystem::path data_dir, const corevmoc::config& corevmoc_config, const chainbase::database& db)
   : cc(data_dir, corevmoc_config, db), exec(cc), mem(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size) {
}

corevmoc_runtime::~corevmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> corevmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size,
                                                                                        const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {
   return std::make_unique<corevmoc_instantiated_module>(code_hash, vm_type, *this);
}

void corevmoc_runtime::init_thread_local_data() {
   exec_thread_local = std::make_unique<corevmoc::executor>(cc);
   mem_thread_local  = std::make_unique<corevmoc::memory>(corevmoc::memory::sliced_pages_for_ro_thread);
}

thread_local std::unique_ptr<corevmoc::executor> corevmoc_runtime::exec_thread_local{};
thread_local std::unique_ptr<corevmoc::memory> corevmoc_runtime::mem_thread_local{};

}}}}
