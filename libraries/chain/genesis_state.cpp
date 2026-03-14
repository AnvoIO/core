#include <core_net/chain/genesis_state.hpp>

// these are required to serialize a genesis_state

namespace core_net { namespace chain {

genesis_state::genesis_state() {
   initial_timestamp = fc::time_point::from_iso_string( "2018-06-01T12:00:00" );
   initial_key = fc::variant(core_net_root_key).as<public_key_type>();
}

chain::chain_id_type genesis_state::compute_chain_id() const {
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return chain_id_type{enc.result()};
}

} } // namespace core_net::chain

// Custom JSON serialization for genesis_state.
// The system_account_prefix field is NOT part of FC_REFLECT (preserving
// binary format / chain_id), so we handle it manually for JSON.
namespace fc {

void to_variant(const core_net::chain::genesis_state& gs, fc::variant& v) {
   fc::mutable_variant_object mvo;
   mvo("initial_timestamp", gs.initial_timestamp);
   mvo("initial_key", gs.initial_key);
   mvo("initial_configuration", gs.initial_configuration);
   if (gs.system_account_prefix) {
      mvo("system_account_prefix", *gs.system_account_prefix);
   }
   v = std::move(mvo);
}

void from_variant(const fc::variant& v, core_net::chain::genesis_state& gs) {
   const auto& vo = v.get_object();
   if (vo.contains("initial_timestamp"))
      fc::from_variant(vo["initial_timestamp"], gs.initial_timestamp);
   if (vo.contains("initial_key"))
      fc::from_variant(vo["initial_key"], gs.initial_key);
   if (vo.contains("initial_configuration"))
      fc::from_variant(vo["initial_configuration"], gs.initial_configuration);
   if (vo.contains("system_account_prefix")) {
      core_net::chain::name prefix;
      fc::from_variant(vo["system_account_prefix"], prefix);
      gs.system_account_prefix = prefix;
   }
}

} // namespace fc
