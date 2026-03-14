#pragma once

#include <core_net/action.hpp>
#include <core_net/crypto.hpp>
#include <core_net/print.hpp>
#include <core_net/privileged.hpp>
#include <core_net/producer_schedule.hpp>
#include <core_net/contract.hpp>
#include <core_net/ignore.hpp>
#include <core_net/system.hpp>

namespace eosiosystem {
using core_net::name;
using core_net::permission_level;
using core_net::public_key;
using core_net::ignore;
using core_net::checksum256;

struct permission_level_weight {
   permission_level  permission;
   uint16_t          weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE( permission_level_weight, (permission)(weight) )
};

struct key_weight {
   core_net::public_key  key;
   uint16_t           weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE( key_weight, (key)(weight) )
};

struct wait_weight {
   uint32_t           wait_sec;
   uint16_t           weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE( wait_weight, (wait_sec)(weight) )
};

struct authority {
   uint32_t                              threshold = 0;
   std::vector<key_weight>               keys;
   std::vector<permission_level_weight>  accounts;
   std::vector<wait_weight>              waits;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE( authority, (threshold)(keys)(accounts)(waits) )
};

struct block_header {
   uint32_t                                  timestamp;
   name                                      producer;
   uint16_t                                  confirmed = 0;
   checksum256                               previous;
   checksum256                               transaction_mroot;
   checksum256                               action_mroot;
   uint32_t                                  schedule_version = 0;
   std::optional<core_net::producer_schedule>   new_producers;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(block_header, (timestamp)(producer)(confirmed)(previous)(transaction_mroot)(action_mroot)
         (schedule_version)(new_producers))
};


struct [[core_net::table("abihash"), core_net::contract("eosio.system")]] abi_hash {
   name              owner;
   checksum256       hash;
   uint64_t primary_key()const { return owner.value; }

   EOSLIB_SERIALIZE( abi_hash, (owner)(hash) )
};

/*
 * Method parameters commented out to prevent generation of code that parses input data.
 */
class [[core_net::contract("eosio.system")]] native : public core_net::contract {
public:

   using core_net::contract::contract;

   /**
    *  Called after a new account is created. This code enforces resource-limits rules
    *  for new accounts as well as new account naming conventions.
    *
    *  1. accounts cannot contain '.' symbols which forces all acccounts to be 12
    *  characters long without '.' until a future account auction process is implemented
    *  which prevents name squatting.
    *
    *  2. new accounts must stake a minimal number of tokens (as set in system parameters)
    *     therefore, this method will execute an inline buyram from receiver for newacnt in
    *     an amount equal to the current new account creation fee.
    */
   [[core_net::action]]
   void newaccount( name             creator,
                    name             name,
                    ignore<authority> owner,
                    ignore<authority> active);


   [[core_net::action]]
   void updateauth(  ignore<name>  account,
                     ignore<name>  permission,
                     ignore<name>  parent,
                     ignore<authority> auth ) {}

   [[core_net::action]]
   void deleteauth( ignore<name>  account,
                    ignore<name>  permission ) {}

   [[core_net::action]]
   void linkauth(  ignore<name>    account,
                   ignore<name>    code,
                   ignore<name>    type,
                   ignore<name>    requirement  ) {}

   [[core_net::action]]
   void unlinkauth( ignore<name>  account,
                    ignore<name>  code,
                    ignore<name>  type ) {}

   [[core_net::action]]
   void canceldelay( ignore<permission_level> canceling_auth, ignore<checksum256> trx_id ) {}

   [[core_net::action]]
   void onerror( ignore<uint128_t> sender_id, ignore<std::vector<char>> sent_trx ) {}

   [[core_net::action]]
   void setabi( name account, const std::vector<char>& abi );

   [[core_net::action]]
   void setcode( name account, uint8_t vmtype, uint8_t vmversion, const std::vector<char>& code ) {}
};
}
