#include <limits>

#include <core_net/action.hpp>
#include <core_net/eosio.hpp>
#include <core_net/permission.hpp>
#include <core_net/print.hpp>
#include <core_net/serialize.hpp>

#include "test_api.hpp"



struct check_auth_msg {
   core_net::name                    account;
   core_net::name                    permission;
   std::vector<core_net::public_key> pubkeys;

   EOSLIB_SERIALIZE( check_auth_msg, (account)(permission)(pubkeys)  )
};

void test_permission::check_authorization( uint64_t receiver, uint64_t code, uint64_t action ) {
   (void)code;
   (void)action;
   using namespace core_net;

   auto self = receiver;
   auto params = unpack_action_data<check_auth_msg>();
   auto packed_pubkeys = pack(params.pubkeys);
   int64_t res64 = core_net::check_permission_authorization( params.account,
                                                     params.permission,
                                                     packed_pubkeys.data(), packed_pubkeys.size(),
                                                     (const char*)0,        0,
                                                     microseconds{ std::numeric_limits<int64_t>::max() }
                                                   );

   auto itr = db_lowerbound_i64( self, self, self, 1 );
   if(itr == -1) {
      db_store_i64( self, self, self, 1, &res64, sizeof(int64_t) );
   } else {
      db_update_i64( itr, self, &res64, sizeof(int64_t) );
   }
}

struct test_permission_last_used_msg {
   core_net::name account;
   core_net::name permission;
   int64_t     last_used_time;

   EOSLIB_SERIALIZE( test_permission_last_used_msg, (account)(permission)(last_used_time) )
};

void test_permission::test_permission_last_used( uint64_t /* receiver */, uint64_t code, uint64_t action ) {
   (void)code;
   (void)action;
   using namespace core_net;

   auto params = unpack_action_data<test_permission_last_used_msg>();

   time_point msec{ microseconds{params.last_used_time}};
   eosio_assert( core_net::get_permission_last_used(params.account, params.permission) == msec, "unexpected last used permission time" );
}

void test_permission::test_account_creation_time( uint64_t /* receiver */, uint64_t code, uint64_t action ) {
   (void)code;
   (void)action;
   using namespace core_net;

   auto params = unpack_action_data<test_permission_last_used_msg>();

   time_point msec{ microseconds{params.last_used_time}};
   eosio_assert( core_net::get_account_creation_time(params.account) == msec, "unexpected account creation time" );
}
