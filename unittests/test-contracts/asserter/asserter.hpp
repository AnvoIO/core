#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] asserter : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void procassert( int8_t condition, std::string message );

   [[core_net::action]]
   void provereset();
};
