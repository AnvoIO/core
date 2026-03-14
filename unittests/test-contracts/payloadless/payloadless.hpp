#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] payloadless : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void doit();

   [[core_net::action]]
   void doitslow();

   [[core_net::action]]
   void doitforever();
};
