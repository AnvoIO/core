#pragma once

#include <core_net/eosio.hpp>
#include <core_net/singleton.hpp>
#include <core_net/asset.hpp>

// Extacted from eosio.token contract:
namespace core_net {
   class [[core_net::contract("eosio.token")]] token : public core_net::contract {
   public:
      using core_net::contract::contract;

      [[core_net::action]]
      void transfer( core_net::name        from,
                     core_net::name        to,
                     core_net::asset       quantity,
                     const std::string& memo );
      using transfer_action = core_net::action_wrapper<"transfer"_n, &token::transfer>;
   };
}

// This contract:
class [[core_net::contract]] proxy : public core_net::contract {
public:
   proxy( core_net::name self, core_net::name first_receiver, core_net::datastream<const char*> ds );

   [[core_net::action]]
   void setowner( core_net::name owner, uint32_t delay );

   [[core_net::on_notify("eosio.token::transfer")]]
   void on_transfer( core_net::name        from,
                     core_net::name        to,
                     core_net::asset       quantity,
                     const std::string& memo );

   [[core_net::on_notify("core_net::onerror")]]
   void on_error( uint128_t sender_id, core_net::ignore<std::vector<char>> sent_trx );

   struct [[core_net::table]] config {
      core_net::name owner;
      uint32_t    delay   = 0;
      uint32_t    next_id = 0;

      EOSLIB_SERIALIZE( config, (owner)(delay)(next_id) )
   };

   using config_singleton = core_net::singleton< "config"_n,  config >;

protected:
   config_singleton _config;
};
