#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] get_sender_test : public core_net::contract {
public:
   using core_net::contract::contract;

   [[core_net::action]]
   void assertsender( core_net::name expected_sender );
   using assertsender_action = core_net::action_wrapper<"assertsender"_n, &get_sender_test::assertsender>;

   [[core_net::action]]
   void sendinline( core_net::name to, core_net::name expected_sender );

   [[core_net::action]]
   void notify( core_net::name to, core_net::name expected_sender, bool send_inline );

   [[core_net::on_notify("*::notify")]]
   void on_notify( core_net::name to, core_net::name expected_sender, bool send_inline );

};
