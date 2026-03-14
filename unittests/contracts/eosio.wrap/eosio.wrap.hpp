#pragma once

#include <core_net/eosio.hpp>
#include <core_net/ignore.hpp>
#include <core_net/transaction.hpp>

namespace core_net {

class [[core_net::contract("eosio.wrap")]] wrap : public contract {
public:
   using contract::contract;

   [[core_net::action]]
   void exec( ignore<name> executer, ignore<transaction> trx );

};

} /// namespace core_net
