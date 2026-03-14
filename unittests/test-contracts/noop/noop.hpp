#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] noop : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void anyaction( core_net::name                       from,
                   const core_net::ignore<std::string>& type,
                   const core_net::ignore<std::string>& data );
};
