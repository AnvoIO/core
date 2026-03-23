#include <core_net/chain/webassembly/interface.hpp>
#include <core_net/chain/apply_context.hpp>
#include <core_net/chain/global_property_object.hpp>
#include <cstdio>

namespace core_net { namespace chain { namespace webassembly {
   int32_t interface::read_action_data(legacy_span<char> memory) const {
      auto s = context.get_action().data.size();
      auto copy_size = std::min( static_cast<size_t>(memory.size()), s );
      if( copy_size == 0 ) return s;
      std::memcpy( memory.data(), context.get_action().data.data(), copy_size );

      // DEBUG: dump action data for setfinalizer actions (large action data typical of BLS keys)
      if( copy_size > 200 && copy_size < 2000 ) {
         static int dump_count = 0;
         if( dump_count < 3 ) {
            dump_count++;
            fprintf(stderr, "\n=== DEBUG read_action_data: size=%zu, copy_size=%zu, wasm_dest_ptr=%p ===\n",
                    s, copy_size, (void*)memory.data());
            const auto* data = reinterpret_cast<const unsigned char*>(context.get_action().data.data());
            for(size_t i = 0; i < copy_size; i += 32) {
               fprintf(stderr, "%04zx: ", i);
               for(size_t j = 0; j < 32 && (i+j) < copy_size; j++) {
                  fprintf(stderr, "%02x", data[i+j]);
               }
               fprintf(stderr, "\n");
            }
            fprintf(stderr, "=== END read_action_data ===\n\n");
         }
      }

      return copy_size;
   }

   int32_t interface::action_data_size() const {
      return context.get_action().data.size();
   }

   name interface::current_receiver() const {
      return context.get_receiver();
   }

   void interface::set_action_return_value( span<const char> packed_blob ) {
      auto max_action_return_value_size = 
         context.control.get_global_properties().configuration.max_action_return_value_size;
      if( !context.trx_context.is_read_only() )
         EOS_ASSERT(packed_blob.size() <= max_action_return_value_size,
                    action_return_value_exception,
                    "action return value size must be less or equal to ${s} bytes", ("s", max_action_return_value_size));
      context.action_return_value.assign( packed_blob.data(), packed_blob.data() + packed_blob.size() );
   }
}}} // ns core_net::chain::webassembly
