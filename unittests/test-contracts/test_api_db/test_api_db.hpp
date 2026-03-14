#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] test_api_db : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action("pg")]]
   void primary_i64_general();

   [[core_net::action("pl")]]
   void primary_i64_lowerbound();

   [[core_net::action("pu")]]
   void primary_i64_upperbound();

   [[core_net::action("s1g")]]
   void idx64_general();

   [[core_net::action("s1l")]]
   void idx64_lowerbound();

   [[core_net::action("s1u")]]
   void idx64_upperbound();

   [[core_net::action("tia")]]
   void test_invalid_access( core_net::name code, uint64_t val, uint32_t index, bool store );

   [[core_net::action("sdnancreate")]]
   void idx_double_nan_create_fail();

   [[core_net::action("sdnanmodify")]]
   void idx_double_nan_modify_fail();

   [[core_net::action("sdnanlookup")]]
   void idx_double_nan_lookup_fail( uint32_t lookup_type );

   [[core_net::action("sk32align")]]
   void misaligned_secondary_key256_tests();

};
