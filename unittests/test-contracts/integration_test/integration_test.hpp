#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] integration_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void store( core_net::name from, core_net::name to, uint64_t num );

   struct [[core_net::table("payloads")]] payload {
      uint64_t              key;
      std::vector<uint64_t> data;

      uint64_t primary_key()const { return key; }

      EOSLIB_SERIALIZE( payload, (key)(data) )
   };

   using payloads_table = core_net::multi_index< "payloads"_n,  payload >;

};
