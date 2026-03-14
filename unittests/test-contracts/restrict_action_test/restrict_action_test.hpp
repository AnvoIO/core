#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] restrict_action_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void noop( );

   [[core_net::action]]
   void sendinline( core_net::name authorizer );

   [[core_net::action]]
   void senddefer( core_net::name authorizer, uint32_t senderid );


   [[core_net::action]]
   void notifyinline( core_net::name acctonotify, core_net::name authorizer );

   [[core_net::action]]
   void notifydefer( core_net::name acctonotify, core_net::name authorizer, uint32_t senderid );

   [[core_net::on_notify("testacc::notifyinline")]]
   void on_notify_inline( core_net::name acctonotify, core_net::name authorizer );

   [[core_net::on_notify("testacc::notifydefer")]]
   void on_notify_defer( core_net::name acctonotify, core_net::name authorizer, uint32_t senderid );
};
