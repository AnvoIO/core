#include <core_net/chain/webassembly/interface.hpp>
#include <core_net/chain/apply_context.hpp>
#include <core_net/vm/allocator.hpp>
#include <cstdio>

namespace core_net { namespace chain { namespace webassembly {
   inline static constexpr size_t max_assert_message = 1024;
   void interface::abort() const {
      EOS_ASSERT( false, abort_called, "abort() called" );
   }

   void interface::core_net_assert( bool condition, null_terminated_ptr msg ) const {
      if( BOOST_UNLIKELY( !condition ) ) {
         const size_t sz = strnlen( msg.data(), max_assert_message );
         std::string message( msg.data(), sz );
         EOS_THROW( core_net_assert_message_exception, "assertion failure with message: ${s}", ("s",message) );
      }
   }

   void interface::core_net_assert_message( bool condition, legacy_span<const char> msg ) const {
      if( BOOST_UNLIKELY( !condition ) ) {
         const size_t sz = msg.size() > max_assert_message ? max_assert_message : msg.size();
         std::string message( msg.data(), sz );

         // DEBUG: trap into debugger when base64 decode size assertion fires
         if( message.find("decoded size") != std::string::npos ) {
            fprintf(stderr, "\n=== JIT DEBUG: assertion fired: %s ===\n", message.c_str());
            try {
               auto* base = context.control.get_wasm_allocator().get_base_ptr<const unsigned char>();
               fprintf(stderr, "WASM linear memory base: %p\n", (void*)base);
               // Dump the string metadata area and decoded data
               // The decoded string data is near 0x34a0 based on previous dumps
               for(size_t start : {(size_t)0x3020, (size_t)0x3480, (size_t)0x34a0}) {
                  fprintf(stderr, "%06zx: ", start);
                  for(size_t j = 0; j < 64; j++) {
                     fprintf(stderr, "%02x", base[start+j]);
                  }
                  fprintf(stderr, "\n");
               }
            } catch(...) {}
         }

         EOS_THROW( core_net_assert_message_exception, "assertion failure with message: ${s}", ("s",message) );
      }
   }

   void interface::core_net_assert_code( bool condition, uint64_t error_code ) const {
      if( BOOST_UNLIKELY( !condition ) ) {
         if( error_code >= static_cast<uint64_t>(system_error_code::generic_system_error) ) {
            restricted_error_code_exception e( FC_LOG_MESSAGE(
                                                   error,
                                                   "core_net_assert_code called with reserved error code: ${error_code}",
                                                   ("error_code", error_code)
            ) );
            e.error_code = static_cast<uint64_t>(system_error_code::contract_restricted_error_code);
            throw e;
         } else {
            core_net_assert_code_exception e( FC_LOG_MESSAGE(
                                             error,
                                             "assertion failure with error code: ${error_code}",
                                             ("error_code", error_code)
            ) );
            e.error_code = error_code;
            throw e;
         }
      }
   }

   //be aware that Core VM OC handles core_net_exit internally and this function will not be called by OC
   void interface::core_net_exit( int32_t code ) const {
      throw wasm_exit{};
   }
}}} // ns core_net::chain::webassembly
