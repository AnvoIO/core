#include <core_net/eosio.hpp>

#include "test_api.hpp"

void test_types::types_size() {

   core_net_assert( sizeof(int64_t)   ==  8, "int64_t size != 8"   );
   core_net_assert( sizeof(uint64_t)  ==  8, "uint64_t size != 8"  );
   core_net_assert( sizeof(uint32_t)  ==  4, "uint32_t size != 4"  );
   core_net_assert( sizeof(int32_t)   ==  4, "int32_t size != 4"   );
   core_net_assert( sizeof(uint128_t) == 16, "uint128_t size != 16");
   core_net_assert( sizeof(int128_t)  == 16, "int128_t size != 16" );
   core_net_assert( sizeof(uint8_t)   ==  1, "uint8_t size != 1"   );

   core_net_assert( sizeof(core_net::name) ==  8, "name size !=  8");
}

void test_types::char_to_symbol() {

   core_net_assert( core_net::name::char_to_value('1') ==  1, "core_net::char_to_symbol('1') !=  1" );
   core_net_assert( core_net::name::char_to_value('2') ==  2, "core_net::char_to_symbol('2') !=  2" );
   core_net_assert( core_net::name::char_to_value('3') ==  3, "core_net::char_to_symbol('3') !=  3" );
   core_net_assert( core_net::name::char_to_value('4') ==  4, "core_net::char_to_symbol('4') !=  4" );
   core_net_assert( core_net::name::char_to_value('5') ==  5, "core_net::char_to_symbol('5') !=  5" );
   core_net_assert( core_net::name::char_to_value('a') ==  6, "core_net::char_to_symbol('a') !=  6" );
   core_net_assert( core_net::name::char_to_value('b') ==  7, "core_net::char_to_symbol('b') !=  7" );
   core_net_assert( core_net::name::char_to_value('c') ==  8, "core_net::char_to_symbol('c') !=  8" );
   core_net_assert( core_net::name::char_to_value('d') ==  9, "core_net::char_to_symbol('d') !=  9" );
   core_net_assert( core_net::name::char_to_value('e') == 10, "core_net::char_to_symbol('e') != 10" );
   core_net_assert( core_net::name::char_to_value('f') == 11, "core_net::char_to_symbol('f') != 11" );
   core_net_assert( core_net::name::char_to_value('g') == 12, "core_net::char_to_symbol('g') != 12" );
   core_net_assert( core_net::name::char_to_value('h') == 13, "core_net::char_to_symbol('h') != 13" );
   core_net_assert( core_net::name::char_to_value('i') == 14, "core_net::char_to_symbol('i') != 14" );
   core_net_assert( core_net::name::char_to_value('j') == 15, "core_net::char_to_symbol('j') != 15" );
   core_net_assert( core_net::name::char_to_value('k') == 16, "core_net::char_to_symbol('k') != 16" );
   core_net_assert( core_net::name::char_to_value('l') == 17, "core_net::char_to_symbol('l') != 17" );
   core_net_assert( core_net::name::char_to_value('m') == 18, "core_net::char_to_symbol('m') != 18" );
   core_net_assert( core_net::name::char_to_value('n') == 19, "core_net::char_to_symbol('n') != 19" );
   core_net_assert( core_net::name::char_to_value('o') == 20, "core_net::char_to_symbol('o') != 20" );
   core_net_assert( core_net::name::char_to_value('p') == 21, "core_net::char_to_symbol('p') != 21" );
   core_net_assert( core_net::name::char_to_value('q') == 22, "core_net::char_to_symbol('q') != 22" );
   core_net_assert( core_net::name::char_to_value('r') == 23, "core_net::char_to_symbol('r') != 23" );
   core_net_assert( core_net::name::char_to_value('s') == 24, "core_net::char_to_symbol('s') != 24" );
   core_net_assert( core_net::name::char_to_value('t') == 25, "core_net::char_to_symbol('t') != 25" );
   core_net_assert( core_net::name::char_to_value('u') == 26, "core_net::char_to_symbol('u') != 26" );
   core_net_assert( core_net::name::char_to_value('v') == 27, "core_net::char_to_symbol('v') != 27" );
   core_net_assert( core_net::name::char_to_value('w') == 28, "core_net::char_to_symbol('w') != 28" );
   core_net_assert( core_net::name::char_to_value('x') == 29, "core_net::char_to_symbol('x') != 29" );
   core_net_assert( core_net::name::char_to_value('y') == 30, "core_net::char_to_symbol('y') != 30" );
   core_net_assert( core_net::name::char_to_value('z') == 31, "core_net::char_to_symbol('z') != 31" );

   for(unsigned char i = 0; i<255; i++) {
      if( (i >= 'a' && i <= 'z') || (i >= '1' || i <= '5') ) continue;
      core_net_assert( core_net::name::char_to_value((char)i) == 0, "core_net::char_to_symbol() != 0" );
   }
}

void test_types::string_to_name() {
   return;
   core_net_assert( core_net::name("a") == "a"_n, "core_net::string_to_name(a)" );
   core_net_assert( core_net::name("ba") == "ba"_n, "core_net::string_to_name(ba)" );
   core_net_assert( core_net::name("cba") == "cba"_n, "core_net::string_to_name(cba)" );
   core_net_assert( core_net::name("dcba") == "dcba"_n, "core_net::string_to_name(dcba)" );
   core_net_assert( core_net::name("edcba") == "edcba"_n, "core_net::string_to_name(edcba)" );
   core_net_assert( core_net::name("fedcba") == "fedcba"_n, "core_net::string_to_name(fedcba)" );
   core_net_assert( core_net::name("gfedcba") == "gfedcba"_n, "core_net::string_to_name(gfedcba)" );
   core_net_assert( core_net::name("hgfedcba") == "hgfedcba"_n, "core_net::string_to_name(hgfedcba)" );
   core_net_assert( core_net::name("ihgfedcba") == "ihgfedcba"_n, "core_net::string_to_name(ihgfedcba)" );
   core_net_assert( core_net::name("jihgfedcba") == "jihgfedcba"_n, "core_net::string_to_name(jihgfedcba)" );
   core_net_assert( core_net::name("kjihgfedcba") == "kjihgfedcba"_n, "core_net::string_to_name(kjihgfedcba)" );
   core_net_assert( core_net::name("lkjihgfedcba") == "lkjihgfedcba"_n, "core_net::string_to_name(lkjihgfedcba)" );
   core_net_assert( core_net::name("mlkjihgfedcba") == "mlkjihgfedcba"_n, "core_net::string_to_name(mlkjihgfedcba)" );
}
