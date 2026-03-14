#pragma once

#include <core_net/asset.hpp>
#include <core_net/eosio.hpp>

#include <string>

namespace eosiosystem {
class system_contract;
}

namespace core_net {

using std::string;

class [[core_net::contract("eosio.token")]] token : public contract {
public:
   using contract::contract;

   [[core_net::action]]
   void create( name   issuer,
                asset  maximum_supply);

   [[core_net::action]]
   void issue( name to, asset quantity, string memo );

   [[core_net::action]]
   void retire( asset quantity, string memo );

   [[core_net::action]]
   void transfer( name    from,
                  name    to,
                  asset   quantity,
                  string  memo );

   [[core_net::action]]
   void open( name owner, const symbol& symbol, name ram_payer );

   [[core_net::action]]
   void close( name owner, const symbol& symbol );

   static asset get_supply( name token_contract_account, symbol_code sym_code )
   {
      stats statstable( token_contract_account, sym_code.raw() );
      const auto& st = statstable.get( sym_code.raw() );
      return st.supply;
   }

   static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
   {
      accounts accountstable( token_contract_account, owner.value );
      const auto& ac = accountstable.get( sym_code.raw() );
      return ac.balance;
   }

private:
   struct [[core_net::table]] account {
      asset    balance;

      uint64_t primary_key()const { return balance.symbol.code().raw(); }
   };

   struct [[core_net::table]] currency_stats {
      asset    supply;
      asset    max_supply;
      name     issuer;

      uint64_t primary_key()const { return supply.symbol.code().raw(); }
   };

   typedef core_net::multi_index< "accounts"_n, account > accounts;
   typedef core_net::multi_index< "stat"_n, currency_stats > stats;

   void sub_balance( name owner, asset value );
   void add_balance( name owner, asset value, name ram_payer );
};

} /// namespace core_net
