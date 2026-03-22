#include <core_net/wasm_backend/backend.hpp>
#include <core_net/vm/watchdog.hpp>

using namespace core_net;
using namespace core_net::wasm_backend;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ) {
   wasm_allocator wa;
   wasm_code wc; 
   wc.resize(size);
   memcpy((uint8_t*)wc.data(), data, size);
   backend<std::nullptr_t> bkend( wc );
   bkend.execute_all(null_watchdog());
}
