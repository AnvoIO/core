#include <core_net/chain/webassembly/interface.hpp>
#include <core_net/chain/apply_context.hpp>

namespace core_net { namespace chain { namespace webassembly {
   int32_t interface::get_context_free_data( uint32_t index, legacy_span<char> buffer) const {
      return context.get_context_free_data( index, buffer.data(), buffer.size() );
   }
}}} // ns core_net::chain::webassembly
