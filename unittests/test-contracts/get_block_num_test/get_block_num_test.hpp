#pragma once

#include <core_net/eosio.hpp>

using bytes = std::vector<char>;

namespace core_net {
   namespace internal_use_do_not_use {
      extern "C" {
         __attribute__((eosio_wasm_import))
         uint32_t get_block_num(); 
      }
   }
}

class [[core_net::contract]] get_block_num_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void testblock(uint32_t expected_result);
};
