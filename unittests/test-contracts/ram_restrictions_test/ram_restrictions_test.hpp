#pragma once

#include <core_net/eosio.hpp>

class [[core_net::contract]] ram_restrictions_test : public core_net::contract {
public:
   struct [[core_net::table]] data {
      uint64_t           key;
      std::vector<char>  value;

      uint64_t primary_key() const { return key; }
   };

   typedef core_net::multi_index<"tablea"_n, data> tablea;
   typedef core_net::multi_index<"tableb"_n, data> tableb;

public:
   using core_net::contract::contract;

   [[core_net::action]]
   void noop();

   [[core_net::action]]
   void setdata( uint32_t len1, uint32_t len2, core_net::name payer );

   [[core_net::action]]
   void notifysetdat( core_net::name acctonotify, uint32_t len1, uint32_t len2, core_net::name payer );

   [[core_net::on_notify("tester2::notifysetdat")]]
   void on_notify_setdata( core_net::name acctonotify, uint32_t len1, uint32_t len2, core_net::name payer );

   [[core_net::action]]
   void senddefer( uint64_t senderid, core_net::name payer );

   [[core_net::action]]
   void notifydefer( core_net::name acctonotify, uint64_t senderid, core_net::name payer );

   [[core_net::on_notify("tester2::notifydefer")]]
   void on_notifydefer( core_net::name acctonotify, uint64_t senderid, core_net::name payer );

};
