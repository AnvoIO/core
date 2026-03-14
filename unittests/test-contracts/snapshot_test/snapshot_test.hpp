#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] snapshot_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void increment( uint32_t value );

   struct [[core_net::table("data")]] main_record {
      uint64_t           id         = 0;
      double             index_f64  = 0.0;
      long double        index_f128 = 0.0L;
      uint64_t           index_i64  = 0ULL;
      uint128_t          index_i128 = 0ULL;
      core_net::checksum256 index_i256;

      uint64_t                  primary_key()const    { return id; }
      double                    get_index_f64()const  { return index_f64 ; }
      long double               get_index_f128()const { return index_f128; }
      uint64_t                  get_index_i64()const  { return index_i64 ; }
      uint128_t                 get_index_i128()const { return index_i128; }
      const core_net::checksum256& get_index_i256()const { return index_i256; }

      EOSLIB_SERIALIZE( main_record, (id)(index_f64)(index_f128)(index_i64)(index_i128)(index_i256) )
   };

   using data_table = core_net::multi_index<"data"_n, main_record,
      core_net::indexed_by< "byf"_n,    core_net::const_mem_fun< main_record, double,
                                                           &main_record::get_index_f64 > >,
      core_net::indexed_by< "byff"_n,   core_net::const_mem_fun< main_record, long double,
                                                           &main_record::get_index_f128> >,
      core_net::indexed_by< "byi"_n,    core_net::const_mem_fun< main_record, uint64_t,
                                                           &main_record::get_index_i64 > >,
      core_net::indexed_by< "byii"_n,   core_net::const_mem_fun< main_record, uint128_t,
                                                           &main_record::get_index_i128 > >,
      core_net::indexed_by< "byiiii"_n, core_net::const_mem_fun< main_record, const core_net::checksum256&,
                                                           &main_record::get_index_i256 > >
   >;

   struct [[core_net::table("test")]] test_record {
      uint64_t id = 0;
      core_net::checksum256 payload;
      uint64_t primary_key() const {return id;}
   };
   using test_table = core_net::multi_index<"test"_n, test_record>;
   [[core_net::action]] void add(core_net::name scope, uint64_t id, core_net::checksum256 payload);
   [[core_net::action]] void remove(core_net::name scope, uint64_t id);
   [[core_net::action]] void verify(core_net::name scope, uint64_t id, core_net::checksum256 payload);
};
