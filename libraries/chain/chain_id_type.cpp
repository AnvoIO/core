#include <core_net/chain/chain_id_type.hpp>
#include <core_net/chain/exceptions.hpp>

namespace core_net { namespace chain {

   void chain_id_type::reflector_init()const {
      CORE_ASSERT( *reinterpret_cast<const fc::sha256*>(this) != fc::sha256(), chain_id_type_exception, "chain_id_type cannot be zero" );
   }

} }  // namespace core_net::chain

namespace fc {

   void to_variant(const core_net::chain::chain_id_type& cid, fc::variant& v) {
      to_variant( static_cast<const fc::sha256&>(cid), v);
   }

   void from_variant(const fc::variant& v, core_net::chain::chain_id_type& cid) {
      from_variant( v, static_cast<fc::sha256&>(cid) );
   }

} // fc
