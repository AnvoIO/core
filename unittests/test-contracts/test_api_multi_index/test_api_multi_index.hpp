#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] test_api_multi_index : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action("s1g")]]
   void idx64_general();

   [[core_net::action("s1store")]]
   void idx64_store_only();

   [[core_net::action("s1check")]]
   void idx64_check_without_storing();

   [[core_net::action("s1findfail1")]]
   void idx64_require_find_fail();

   [[core_net::action("s1findfail2")]]
   void idx64_require_find_fail_with_msg();

   [[core_net::action("s1findfail3")]]
   void idx64_require_find_sk_fail();

   [[core_net::action("s1findfail4")]]
   void idx64_require_find_sk_fail_with_msg();

   [[core_net::action("s1pkend")]]
   void idx64_pk_iterator_exceed_end();

   [[core_net::action("s1skend")]]
   void idx64_sk_iterator_exceed_end();

   [[core_net::action("s1pkbegin")]]
   void idx64_pk_iterator_exceed_begin();

   [[core_net::action("s1skbegin")]]
   void idx64_sk_iterator_exceed_begin();

   [[core_net::action("s1pkref")]]
   void idx64_pass_pk_ref_to_other_table();

   [[core_net::action("s1skref")]]
   void idx64_pass_sk_ref_to_other_table();

   [[core_net::action("s1pkitrto")]]
   void idx64_pass_pk_end_itr_to_iterator_to();

   [[core_net::action("s1pkmodify")]]
   void idx64_pass_pk_end_itr_to_modify();

   [[core_net::action("s1pkerase")]]
   void idx64_pass_pk_end_itr_to_erase();

   [[core_net::action("s1skitrto")]]
   void idx64_pass_sk_end_itr_to_iterator_to();

   [[core_net::action("s1skmodify")]]
   void idx64_pass_sk_end_itr_to_modify();

   [[core_net::action("s1skerase")]]
   void idx64_pass_sk_end_itr_to_erase();

   [[core_net::action("s1modpk")]]
   void idx64_modify_primary_key();

   [[core_net::action("s1exhaustpk")]]
   void idx64_run_out_of_avl_pk();

   [[core_net::action("s1skcache")]]
   void idx64_sk_cache_pk_lookup();

   [[core_net::action("s1pkcache")]]
   void idx64_pk_cache_sk_lookup();

   [[core_net::action("s2g")]]
   void idx128_general();

   [[core_net::action("s2store")]]
   void idx128_store_only();

   [[core_net::action("s2check")]]
   void idx128_check_without_storing();

   [[core_net::action("s2autoinc")]]
   void idx128_autoincrement_test();

   [[core_net::action("s2autoinc1")]]
   void idx128_autoincrement_test_part1();

   [[core_net::action("s2autoinc2")]]
   void idx128_autoincrement_test_part2();

   [[core_net::action("s3g")]]
   void idx256_general();

   [[core_net::action("sdg")]]
   void idx_double_general();

   [[core_net::action("sldg")]]
   void idx_long_double_general();

};
