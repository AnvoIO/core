#pragma once

#include <core_net/chain/types.hpp>

namespace core_net { namespace chain {

   using whitelisted_intrinsics_type = shared_flat_multimap<uint64_t, shared_string>;

   bool is_intrinsic_whitelisted( const whitelisted_intrinsics_type& whitelisted_intrinsics, std::string_view name );

   void add_intrinsic_to_whitelist( whitelisted_intrinsics_type& whitelisted_intrinsics, std::string_view name );

   void remove_intrinsic_from_whitelist( whitelisted_intrinsics_type& whitelisted_intrinsics, std::string_view name );

   void reset_intrinsic_whitelist( whitelisted_intrinsics_type& whitelisted_intrinsics,
                                   const std::set<std::string>& s );

   std::set<std::string> convert_intrinsic_whitelist_to_set( const whitelisted_intrinsics_type& whitelisted_intrinsics );

} } // namespace core_net::chain
