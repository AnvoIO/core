#include <core_net/chain/controller.hpp>
#include <core_net/chain/config.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/permission_object.hpp>
#include <core_net/testing/tester.hpp>

using namespace core_net;
using namespace chain;
using namespace core_net::testing;

BOOST_AUTO_TEST_SUITE(system_accounts_tests)

// ---- Test 1: Default prefix is "eosio" ----
BOOST_AUTO_TEST_CASE( default_prefix_is_eosio )
{ try {
   config::reset_system_accounts_for_testing();
   legacy_tester test(setup_policy::none);

   // The default genesis should set "eosio" as the system account
   BOOST_CHECK_EQUAL( config::system_account_name(), "eosio"_n );

   // Verify the system account exists in the database
   const chain::database& db = test.control->db();
   auto sys = db.find<account_object, by_name>(config::system_account_name());
   BOOST_CHECK( sys != nullptr );

   // Verify null account name and existence
   BOOST_CHECK_EQUAL( config::null_account_name(), "eosio.null"_n );
   auto null_acct = db.find<account_object, by_name>(config::null_account_name());
   BOOST_CHECK( null_acct != nullptr );

   // Verify producers account name and existence
   BOOST_CHECK_EQUAL( config::producers_account_name(), "eosio.prods"_n );
   auto prods_acct = db.find<account_object, by_name>(config::producers_account_name());
   BOOST_CHECK( prods_acct != nullptr );

   // Verify prefix string
   BOOST_CHECK_EQUAL( config::system_account_prefix_str(), "eosio." );

} FC_LOG_AND_RETHROW() }

// ---- Test 2: Custom "core" prefix ----
BOOST_AUTO_TEST_CASE( custom_core_prefix )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   // Verify system account name is "core"
   BOOST_CHECK_EQUAL( config::system_account_name(), "core"_n );

   // Verify the "core" account exists
   const chain::database& db = test.control->db();
   auto sys = db.find<account_object, by_name>("core"_n);
   BOOST_CHECK( sys != nullptr );

   // Verify derivative account names
   BOOST_CHECK_EQUAL( config::null_account_name(), "core.null"_n );
   BOOST_CHECK_EQUAL( config::producers_account_name(), "core.prods"_n );
   BOOST_CHECK_EQUAL( config::system_account_prefix_str(), "core." );

   // Verify null and producers accounts exist
   auto null_acct = db.find<account_object, by_name>("core.null"_n);
   BOOST_CHECK( null_acct != nullptr );

   auto prods_acct = db.find<account_object, by_name>("core.prods"_n);
   BOOST_CHECK( prods_acct != nullptr );

} FC_LOG_AND_RETHROW() }

// ---- Test 3: Reserved prefix check with custom prefix ----
BOOST_AUTO_TEST_CASE( reserved_prefix_with_custom )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   // Create a non-privileged account to use as creator
   test.create_account( "alice"_n );
   test.produce_block();

   // Trying to create an account starting with "core." from a non-privileged
   // account should fail
   BOOST_CHECK_EXCEPTION(
      test.create_account( "core.test1"_n, "alice"_n ),
      action_validate_exception,
      fc_exception_message_is("only privileged accounts can have names that start with 'core.'")
   );

   // Creating a normal account (not starting with "core.") should succeed
   test.create_account( "bob"_n, "alice"_n );

   // Creating an account starting with "eosio." should succeed on a "core." chain
   // because "eosio." is NOT the reserved prefix here
   test.create_account( "eosio.test1"_n, "alice"_n );

   // Creating an account starting with "core." from the privileged system account
   // should succeed
   test.create_account( "core.test2"_n );

} FC_LOG_AND_RETHROW() }

// ---- Test 4: system_accounts::from_prefix unit tests ----
BOOST_AUTO_TEST_CASE( from_prefix_unit_test )
{ try {
   // Test from_prefix with "core"
   auto core_sa = config::system_accounts::from_prefix("core"_n);
   BOOST_CHECK_EQUAL( core_sa.system_account,    "core"_n );
   BOOST_CHECK_EQUAL( core_sa.null_account,      "core.null"_n );
   BOOST_CHECK_EQUAL( core_sa.producers_account, "core.prods"_n );
   BOOST_CHECK_EQUAL( core_sa.auth_scope,        "core.auth"_n );
   BOOST_CHECK_EQUAL( core_sa.all_scope,         "core.all"_n );
   BOOST_CHECK_EQUAL( core_sa.any,               "core.any"_n );
   BOOST_CHECK_EQUAL( core_sa.code,              "core.code"_n );

   // Test eosio_defaults
   auto eosio_sa = config::system_accounts::eosio_defaults();
   BOOST_CHECK_EQUAL( eosio_sa.system_account,    "eosio"_n );
   BOOST_CHECK_EQUAL( eosio_sa.null_account,      "eosio.null"_n );
   BOOST_CHECK_EQUAL( eosio_sa.producers_account, "eosio.prods"_n );
   BOOST_CHECK_EQUAL( eosio_sa.auth_scope,        "eosio.auth"_n );
   BOOST_CHECK_EQUAL( eosio_sa.all_scope,         "eosio.all"_n );
   BOOST_CHECK_EQUAL( eosio_sa.any,               "eosio.any"_n );
   BOOST_CHECK_EQUAL( eosio_sa.code,              "eosio.code"_n );

   // Test core_defaults matches from_prefix("core"_n)
   auto core_defaults = config::system_accounts::core_defaults();
   BOOST_CHECK_EQUAL( core_defaults.system_account,    core_sa.system_account );
   BOOST_CHECK_EQUAL( core_defaults.null_account,      core_sa.null_account );
   BOOST_CHECK_EQUAL( core_defaults.producers_account, core_sa.producers_account );
   BOOST_CHECK_EQUAL( core_defaults.auth_scope,        core_sa.auth_scope );
   BOOST_CHECK_EQUAL( core_defaults.all_scope,         core_sa.all_scope );
   BOOST_CHECK_EQUAL( core_defaults.any,               core_sa.any );
   BOOST_CHECK_EQUAL( core_defaults.code,              core_sa.code );

   // Test from_prefix with "eosio" matches eosio_defaults
   auto eosio_from = config::system_accounts::from_prefix("eosio"_n);
   BOOST_CHECK_EQUAL( eosio_from.system_account,    eosio_sa.system_account );
   BOOST_CHECK_EQUAL( eosio_from.null_account,      eosio_sa.null_account );
   BOOST_CHECK_EQUAL( eosio_from.producers_account, eosio_sa.producers_account );
   BOOST_CHECK_EQUAL( eosio_from.auth_scope,        eosio_sa.auth_scope );
   BOOST_CHECK_EQUAL( eosio_from.all_scope,         eosio_sa.all_scope );
   BOOST_CHECK_EQUAL( eosio_from.any,               eosio_sa.any );
   BOOST_CHECK_EQUAL( eosio_from.code,              eosio_sa.code );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
