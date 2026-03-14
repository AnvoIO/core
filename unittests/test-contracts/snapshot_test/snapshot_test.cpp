#include "snapshot_test.hpp"

using namespace core_net;

void snapshot_test::increment( uint32_t value ) {
   require_auth( get_self() );

   data_table data( get_self(), get_self().value );

   auto current = data.begin();
   if( current == data.end() ) {
      data.emplace( get_self(), [&]( auto& r ) {
         r.id         = value;
         r.index_f64  = value;
         r.index_f128 = value;
         r.index_i64  = value;
         r.index_i128 = value;
         r.index_i256.data()[0] = value;
      } );
   } else {
      data.modify( current, same_payer, [&]( auto& r ) {
         r.index_f64  += value;
         r.index_f128 += value;
         r.index_i64  += value;
         r.index_i128 += value;
         r.index_i256.data()[0] += value;
      } );
   }
}

void snapshot_test::add(core_net::name scope, uint64_t id, core_net::checksum256 payload) {
   require_auth(get_self());

   test_table(get_self(), scope.value).emplace(get_self(), [&](test_record& record) {
      record.id = id;
      record.payload = payload;
   });
}

void snapshot_test::remove(core_net::name scope, uint64_t id) {
   require_auth(get_self());

   test_table table(get_self(), scope.value);
   test_table::const_iterator it = table.require_find(id);
   table.erase(it);
}

void snapshot_test::verify(core_net::name scope, uint64_t id, core_net::checksum256 payload) {
   require_auth(get_self());

   test_table table(get_self(), scope.value);
   test_table::const_iterator it = table.require_find(id);
   check(it->payload == payload, "that's not right");
}
