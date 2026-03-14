#pragma once

#include <core_net/eosio.hpp>
#include <vector>

class [[core_net::contract]] deferred_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void defercall( core_net::name payer, uint64_t sender_id, core_net::name contract, uint64_t payload );

   [[core_net::action]]
   void delayedcall( core_net::name payer, uint64_t sender_id, core_net::name contract,
                     uint64_t payload, uint32_t delay_sec, bool replace_existing );

   [[core_net::action]]
   void cancelcall( uint64_t sender_id );

   [[core_net::action]]
   void deferfunc( uint64_t payload );
   using deferfunc_action = core_net::action_wrapper<"deferfunc"_n, &deferred_test::deferfunc>;

   [[core_net::action]]
   void inlinecall( core_net::name contract, core_net::name authorizer, uint64_t payload );

   [[core_net::action]]
   void fail();

   [[core_net::on_notify("core_net::onerror")]]
   void on_error( uint128_t sender_id, core_net::ignore<std::vector<char>> sent_trx );
};
