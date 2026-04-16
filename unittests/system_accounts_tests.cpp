#include <core_net/chain/abi_def.hpp>
#include <core_net/chain/controller.hpp>
#include <core_net/chain/config.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/global_property_object.hpp>
#include <core_net/chain/genesis_state.hpp>
#include <core_net/chain/permission_object.hpp>
#include <core_net/chain/snapshot.hpp>
#include <core_net/state_history/abi.hpp>
#include <core_net/testing/tester.hpp>
#include <fc/io/json.hpp>
#include <boost/test/unit_test.hpp>

#include "snapshot_suites.hpp"
#include <snapshot_tester.hpp>

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

// ---- Test 5: All accessor functions with custom prefix ----
BOOST_AUTO_TEST_CASE( all_accessors_custom_prefix )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   // Verify every accessor returns the correct "core." prefixed name
   BOOST_CHECK_EQUAL( config::system_account_name(),    "core"_n );
   BOOST_CHECK_EQUAL( config::null_account_name(),      "core.null"_n );
   BOOST_CHECK_EQUAL( config::producers_account_name(), "core.prods"_n );
   BOOST_CHECK_EQUAL( config::auth_scope(),             "core.auth"_n );
   BOOST_CHECK_EQUAL( config::all_scope(),              "core.all"_n );
   BOOST_CHECK_EQUAL( config::any_name(),               "core.any"_n );
   BOOST_CHECK_EQUAL( config::code_name(),              "core.code"_n );
   BOOST_CHECK_EQUAL( config::system_account_prefix_str(), "core." );

} FC_LOG_AND_RETHROW() }

// ---- Test 6: Native actions work with custom prefix ----
BOOST_AUTO_TEST_CASE( native_actions_custom_prefix )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   // apply_eosio_newaccount: create accounts under the "core" system account
   test.create_account( "alice"_n );
   test.create_account( "bob"_n );
   test.create_account( "carol"_n );
   test.produce_block();

   // Verify all accounts exist
   const chain::database& db = test.control->db();
   auto alice_acct = db.find<account_object, by_name>("alice"_n);
   auto bob_acct   = db.find<account_object, by_name>("bob"_n);
   auto carol_acct = db.find<account_object, by_name>("carol"_n);
   BOOST_CHECK( alice_acct != nullptr );
   BOOST_CHECK( bob_acct   != nullptr );
   BOOST_CHECK( carol_acct != nullptr );

   // apply_eosio_setabi: set ABI on an account (uses a minimal ABI string)
   const char* minimal_abi = R"({"version":"eosio::abi/1.0"})";
   test.set_abi( "alice"_n, minimal_abi );
   test.produce_block();

   // apply_eosio_updateauth: create a custom permission on an account
   test.set_authority( "bob"_n, "myperm"_n,
      authority( base_tester::get_public_key( "bob"_n, "myperm" ) ),
      "active"_n );
   test.produce_block();

   // apply_eosio_deleteauth: delete the custom permission
   test.delete_authority( "bob"_n, "myperm"_n );
   test.produce_block();

   // Verify chain progresses normally after all native actions
   test.produce_blocks(5);
   BOOST_CHECK( test.head().block_num() > 1 );

} FC_LOG_AND_RETHROW() }

// ---- Test 7: Permission name prefix restriction ----
BOOST_AUTO_TEST_CASE( permission_name_prefix_restriction )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   test.create_account( "alice"_n );
   test.produce_block();

   // Creating a permission starting with "core." on a non-system account should fail
   BOOST_CHECK_EXCEPTION(
      test.set_authority( "alice"_n, "core.test"_n,
         authority( base_tester::get_public_key( "alice"_n, "core.test" ) ),
         "active"_n ),
      action_validate_exception,
      fc_exception_message_starts_with("Permission names that start with 'core.' are reserved")
   );

   // Creating a permission NOT starting with "core." should succeed
   test.set_authority( "alice"_n, "myperm"_n,
      authority( base_tester::get_public_key( "alice"_n, "myperm" ) ),
      "active"_n );
   test.produce_block();

   // Creating a permission starting with "eosio." should succeed on a "core" chain
   // because "eosio." is NOT the reserved prefix here
   test.set_authority( "alice"_n, "eosio.perm"_n,
      authority( base_tester::get_public_key( "alice"_n, "eosio.perm" ) ),
      "active"_n );
   test.produce_block();

} FC_LOG_AND_RETHROW() }

// ---- Test 8: Genesis state system_account_prefix field ----
BOOST_AUTO_TEST_CASE( genesis_system_account_prefix )
{ try {
   // Default genesis should have nullopt prefix
   genesis_state gs_default;
   BOOST_CHECK( !gs_default.system_account_prefix.has_value() );

   // Setting the prefix to "core" should work
   genesis_state gs;
   gs.system_account_prefix = "core"_n;
   BOOST_REQUIRE( gs.system_account_prefix.has_value() );
   BOOST_CHECK_EQUAL( *gs.system_account_prefix, "core"_n );

   // system_account_prefix is NOT part of FC_REFLECT (preserving chain_id),
   // so chain_id should be the same regardless of prefix
   genesis_state gs_core;
   gs_core.system_account_prefix = "core"_n;
   genesis_state gs_none;
   BOOST_CHECK_EQUAL( gs_core.compute_chain_id(), gs_none.compute_chain_id() );

   // Different prefixes should also produce the same chain_id
   genesis_state gs_other;
   gs_other.system_account_prefix = "mynet"_n;
   BOOST_CHECK_EQUAL( gs_other.compute_chain_id(), gs_none.compute_chain_id() );

   // Actually booting a chain with the prefix should propagate it to the GPO
   {
      config::reset_system_accounts_for_testing();

      fc::temp_directory tempdir;
      auto [cfg, genesis] = base_tester::default_config(tempdir);
      genesis.system_account_prefix = "core"_n;
      genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

      legacy_tester test(cfg, genesis);

      // Verify the prefix made it into the GPO
      const auto& gpo = test.control->db().get<global_property_object>();
      BOOST_CHECK_EQUAL( gpo.system_account_prefix, "core"_n );

      // Verify the chain_id matches the genesis (prefix doesn't affect it)
      BOOST_CHECK_EQUAL( test.get_chain_id(), genesis.compute_chain_id() );
   }

} FC_LOG_AND_RETHROW() }

// ---- Test 9: set_system_accounts idempotent + different-prefix guard ----
BOOST_AUTO_TEST_CASE( set_system_accounts_double_call )
{ try {
   // Reset so we start clean
   config::reset_system_accounts_for_testing();

   // First call should succeed
   config::set_system_accounts( config::system_accounts::from_prefix("core"_n) );

   // Second call with SAME prefix should succeed (idempotent)
   BOOST_CHECK_NO_THROW(
      config::set_system_accounts( config::system_accounts::from_prefix("core"_n) )
   );

   // Call with DIFFERENT prefix should throw
   BOOST_CHECK_THROW(
      config::set_system_accounts( config::system_accounts::eosio_defaults() ),
      chain_exception
   );

   // Reset and set to a different prefix — should succeed
   config::reset_system_accounts_for_testing();
   config::set_system_accounts( config::system_accounts::eosio_defaults() );

   // Verify it took effect
   BOOST_CHECK_EQUAL( config::system_account_name(), "eosio"_n );

   // Clean up: reset for subsequent tests
   config::reset_system_accounts_for_testing();

} FC_LOG_AND_RETHROW() }

// ---- Test 10: Global property object persistence ----
BOOST_AUTO_TEST_CASE( gpo_system_account_prefix )
{ try {
   // Test with custom "core" prefix
   {
      config::reset_system_accounts_for_testing();

      fc::temp_directory tempdir;
      auto [cfg, genesis] = base_tester::default_config(tempdir);
      genesis.system_account_prefix = "core"_n;
      genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

      legacy_tester test(cfg, genesis);

      const auto& gpo = test.control->db().get<global_property_object>();
      BOOST_CHECK_EQUAL( gpo.system_account_prefix, "core"_n );
   }

   // Test with default "eosio" prefix
   {
      config::reset_system_accounts_for_testing();
      legacy_tester test(setup_policy::none);

      const auto& gpo = test.control->db().get<global_property_object>();
      BOOST_CHECK_EQUAL( gpo.system_account_prefix, "eosio"_n );
   }

} FC_LOG_AND_RETHROW() }

// ---- Test 11: Snapshot round-trip with custom prefix ----
BOOST_AUTO_TEST_CASE( snapshot_round_trip_custom_prefix )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester chain(cfg, genesis);

   // Create some accounts and produce blocks
   chain.create_account( "alice"_n );
   chain.create_account( "bob"_n );
   chain.produce_blocks(5);

   // Verify prefix before snapshot
   BOOST_CHECK_EQUAL( config::system_account_name(), "core"_n );

   // Take a snapshot
   chain.control->abort_block();
   auto writer = buffered_snapshot_suite::get_writer();
   chain.control->write_snapshot(writer);
   auto snapshot = buffered_snapshot_suite::finalize(writer);

   // Reset system accounts to simulate a fresh process loading the snapshot
   config::reset_system_accounts_for_testing();

   // Restore from snapshot into a new chain
   snapshotted_tester snap_chain(chain.get_config(), buffered_snapshot_suite::get_reader(snapshot), 0);

   // Verify config::system_account_name() returns "core" after restore
   BOOST_CHECK_EQUAL( config::system_account_name(), "core"_n );
   BOOST_CHECK_EQUAL( config::null_account_name(), "core.null"_n );
   BOOST_CHECK_EQUAL( config::producers_account_name(), "core.prods"_n );
   BOOST_CHECK_EQUAL( config::system_account_prefix_str(), "core." );

   // Verify the GPO has the correct prefix
   const auto& gpo = snap_chain.control->db().get<global_property_object>();
   BOOST_CHECK_EQUAL( gpo.system_account_prefix, "core"_n );

   // Verify system accounts exist in the restored chain
   const chain::database& db = snap_chain.control->db();
   auto core_acct  = db.find<account_object, by_name>("core"_n);
   auto null_acct  = db.find<account_object, by_name>("core.null"_n);
   auto prods_acct = db.find<account_object, by_name>("core.prods"_n);
   BOOST_CHECK( core_acct  != nullptr );
   BOOST_CHECK( null_acct  != nullptr );
   BOOST_CHECK( prods_acct != nullptr );

   // Verify user accounts survived the round-trip
   auto alice_acct = db.find<account_object, by_name>("alice"_n);
   auto bob_acct   = db.find<account_object, by_name>("bob"_n);
   BOOST_CHECK( alice_acct != nullptr );
   BOOST_CHECK( bob_acct   != nullptr );

   // Verify integrity hashes match
   chain.control->abort_block();
   const auto orig_hash = chain.control->calculate_integrity_hash();
   const auto snap_hash = snap_chain.control->calculate_integrity_hash();
   BOOST_CHECK_EQUAL( orig_hash.str(), snap_hash.str() );

} FC_LOG_AND_RETHROW() }

// ---- Test 12: System account operations with custom prefix ----
BOOST_AUTO_TEST_CASE( system_account_operations_custom_prefix )
{ try {
   config::reset_system_accounts_for_testing();

   fc::temp_directory tempdir;
   auto [cfg, genesis] = base_tester::default_config(tempdir);
   genesis.system_account_prefix = "core"_n;
   genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );

   legacy_tester test(cfg, genesis);

   // Set ABI on the system account (native setabi action under "core")
   const char* minimal_abi = R"({"version":"eosio::abi/1.0"})";
   test.set_abi( config::system_account_name(), minimal_abi );
   test.produce_block();

   // Verify the system account is "core"
   BOOST_CHECK_EQUAL( config::system_account_name(), "core"_n );

   // Create several accounts — verifies newaccount native action works
   test.create_accounts({ "charlie"_n, "dave"_n, "eve"_n });
   test.produce_block();

   const chain::database& db = test.control->db();
   auto charlie_acct = db.find<account_object, by_name>("charlie"_n);
   auto dave_acct    = db.find<account_object, by_name>("dave"_n);
   auto eve_acct     = db.find<account_object, by_name>("eve"_n);
   BOOST_CHECK( charlie_acct != nullptr );
   BOOST_CHECK( dave_acct    != nullptr );
   BOOST_CHECK( eve_acct     != nullptr );

   // Create a privileged sub-account of the system
   test.create_account( "core.token"_n );
   test.produce_block();

   auto token_acct = db.find<account_object, by_name>("core.token"_n);
   BOOST_CHECK( token_acct != nullptr );

   // Verify that multiple system sub-accounts can coexist
   test.create_account( "core.msig"_n );
   test.produce_block();

   auto msig_acct = db.find<account_object, by_name>("core.msig"_n);
   BOOST_CHECK( msig_acct != nullptr );

   // Produce more blocks to verify chain stability
   test.produce_blocks(10);
   BOOST_CHECK( test.head().block_num() > 10 );

} FC_LOG_AND_RETHROW() }

// ---- Test: ABI version prefix follows chain heritage (issue #105) ----
// system_contract_abi and the SHiP session wire ABI must emit the
// "eosio::abi/*" prefix on eosio-bootstrapped chains so that downstream
// Antelope tooling (abieos, Hyperion, etc.) keeps working without patches.
BOOST_AUTO_TEST_CASE( abi_version_prefix_follows_heritage )
{ try {
   // Legacy (eosio) chain -> emit "eosio::abi/*".
   {
      config::reset_system_accounts_for_testing();
      legacy_tester test(setup_policy::none);
      BOOST_REQUIRE_EQUAL( config::system_account_name(), "eosio"_n );

      const abi_def bundled = core_net_contract_abi(abi_def{});
      BOOST_CHECK_EQUAL( bundled.version, "eosio::abi/1.0" );

      const std::string_view wire = core_net::state_history::session_wire_abi();
      BOOST_CHECK_NE( wire.find(R"("version": "eosio::abi/1.1")"), std::string_view::npos );
      BOOST_CHECK_EQUAL( wire.find(R"("version": "core_net::abi/1.1")"), std::string_view::npos );
   }

   // Fresh (core) chain -> emit "core_net::abi/*".
   {
      config::reset_system_accounts_for_testing();

      fc::temp_directory tempdir;
      auto [cfg, genesis] = base_tester::default_config(tempdir);
      genesis.system_account_prefix = "core"_n;
      genesis.initial_key = base_tester::get_public_key( "core"_n, "active" );
      legacy_tester test(cfg, genesis);
      BOOST_REQUIRE_EQUAL( config::system_account_name(), "core"_n );

      const abi_def bundled = core_net_contract_abi(abi_def{});
      BOOST_CHECK_EQUAL( bundled.version, "core_net::abi/1.0" );

      const std::string_view wire = core_net::state_history::session_wire_abi();
      BOOST_CHECK_NE( wire.find(R"("version": "core_net::abi/1.1")"), std::string_view::npos );
      BOOST_CHECK_EQUAL( wire.find(R"("version": "eosio::abi/1.1")"), std::string_view::npos );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
