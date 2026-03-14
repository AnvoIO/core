#include <core_net/eosio.hpp>

// This creates a simple table without require_auth used.
// It is mainly used to test read-only transactions.

using namespace core_net;
using namespace std;

class [[core_net::contract]] no_auth_table : public contract {
public:
   using contract::contract;

   no_auth_table(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}

   [[core_net::action]]
   uint64_t getage(name user) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      return iterator->age;
   }

   // This is used to create unique transaction by varying id.
   [[core_net::action]]
   uint64_t age(name user, uint64_t id) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator != people.end(), "age: record does not exist");
      return iterator->age;
   }

   [[core_net::action]]
   void insert(name user, uint64_t id, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator == people.end(), "Record already exists");
      people.emplace(user, [&]( auto& row ) {
         row.key = user;
         row.id = id;
         row.age = age;
      });
   }

   [[core_net::action]]
   void modify(name user, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      people.modify(iterator, user, [&]( auto& row ) {
         row.key = user;
         row.age = age;
      });
   }

   [[core_net::action]]
   void modifybyid(uint64_t id, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto secondary_index = people.template get_index<"byid"_n>();
      auto iterator = secondary_index.find(id);
      check(iterator != secondary_index.end(), "Record does not exist");
      secondary_index.modify(iterator, get_self(), [&]( auto& row ) {
         row.id = id;
         row.age = age;
      });
   }
 
   [[core_net::action]]
   void erase(name user) {
      person_index people( get_self(), get_first_receiver().value);

      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      people.erase(iterator);
   } 

   [[core_net::action]]
   void erasebyid(uint64_t id) {
      person_index people( get_self(), get_first_receiver().value );
      auto secondary_index = people.template get_index<"byid"_n>();
      auto iterator = secondary_index.find(id);
      check(iterator != secondary_index.end(), "Record does not exist");
      secondary_index.erase(iterator);
   }

private:
   struct [[core_net::table]] person {
      name key;
      uint64_t id;
      uint64_t age;
      uint64_t primary_key() const { return key.value; }
      uint64_t sec64_key() const { return id; }
   };
   using person_index = core_net::multi_index<"people"_n, person,
     indexed_by<"byid"_n, const_mem_fun<person, uint64_t, &person::sec64_key>>
    >;
};
