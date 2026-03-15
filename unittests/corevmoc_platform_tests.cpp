#include <core_net/testing/tester.hpp>
#include <test_contracts.hpp>
#include <boost/test/unit_test.hpp>

#include <core_net/chain/webassembly/core-vm-oc/gs_seg_helpers.h>
#ifdef CORE_NET_VM_OC_RUNTIME_ENABLED
#include <core_net/chain/webassembly/core-vm-oc/memory.hpp>
#endif

using namespace core_net;
using namespace core_net::chain;
using namespace core_net::testing;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(corevmoc_platform_tests)

// Test that the base register read/write round-trips correctly.
// On x86_64 this exercises GS segment via arch_prctl or FSGSBASE.
// On AArch64 this exercises X28 via inline assembly.
BOOST_AUTO_TEST_CASE( platform_register_readwrite ) { try {
#ifdef CORE_NET_VM_OC_RUNTIME_ENABLED
   // Verify the base register starts at 0 (no executor active)
   // and that set/get round-trips. We must restore 0 afterwards
   // since a non-zero value tells the signal handler we're in WASM.
   uint64_t initial = core_vm_oc_getgs();
   BOOST_CHECK_EQUAL(initial, (uint64_t)0);

   // Write a value, read it back, then restore
   uint64_t test_val = 0x1000; // small safe value
   core_vm_oc_setgs(test_val);
   uint64_t read_val = core_vm_oc_getgs();
   BOOST_CHECK_EQUAL(read_val, test_val);

   // Restore to 0
   core_vm_oc_setgs(0);
   BOOST_CHECK_EQUAL(core_vm_oc_getgs(), (uint64_t)0);
#endif
} FC_LOG_AND_RETHROW() }

// Test that the memory stride and control block offset constants are consistent.
BOOST_AUTO_TEST_CASE( memory_layout_constants ) { try {
#ifdef CORE_NET_VM_OC_RUNTIME_ENABLED
   auto stride_val = core_net::chain::corevmoc::memory::stride;
   BOOST_CHECK_EQUAL(stride_val, (size_t)CORE_NET_VM_OC_MEMORY_STRIDE);
   auto cb_sum = (int64_t)core_net::chain::corevmoc::memory::cb_offset + CORE_NET_VM_OC_CONTROL_BLOCK_OFFSET;
   BOOST_CHECK_EQUAL(cb_sum, (int64_t)0);
#endif
} FC_LOG_AND_RETHROW() }

// End-to-end smoke test: deploy a contract and execute actions.
// When run via CTest with vm-oc runtime, this exercises the full OC pipeline.
// When run without OC, it validates baseline correctness.
BOOST_AUTO_TEST_CASE( oc_contract_smoke ) { try {
   fc::temp_directory tempdir;
   constexpr bool use_genesis = true;
   validating_tester chain(tempdir, [](controller::config&){}, use_genesis);

   chain.create_accounts({"alice"_n, "bob"_n, "eosio.token"_n});
   chain.set_code("eosio.token"_n, test_contracts::core_net_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::core_net_token_abi());
   chain.produce_block();

   // Create token
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mvo()
      ("issuer", "eosio.token")
      ("maximum_supply", "1000000.0000 TOK")
   );

   // Issue tokens
   chain.push_action("eosio.token"_n, "issue"_n, "eosio.token"_n, mvo()
      ("to", "alice")
      ("quantity", "1000.0000 TOK")
      ("memo", "initial")
   );

   // Transfer
   chain.push_action("eosio.token"_n, "transfer"_n, "alice"_n, mvo()
      ("from", "alice")
      ("to", "bob")
      ("quantity", "100.0000 TOK")
      ("memo", "first transfer")
   );

   chain.produce_block();

   // Transfer back
   chain.push_action("eosio.token"_n, "transfer"_n, "bob"_n, mvo()
      ("from", "bob")
      ("to", "alice")
      ("quantity", "50.0000 TOK")
      ("memo", "return transfer")
   );

   chain.produce_block();

   // Verify balances
   auto alice_balance = chain.get_currency_balance("eosio.token"_n, chain::symbol(4, "TOK"), "alice"_n);
   auto bob_balance = chain.get_currency_balance("eosio.token"_n, chain::symbol(4, "TOK"), "bob"_n);
   BOOST_CHECK_EQUAL(alice_balance, chain::asset::from_string("950.0000 TOK"));
   BOOST_CHECK_EQUAL(bob_balance, chain::asset::from_string("50.0000 TOK"));
} FC_LOG_AND_RETHROW() }

// Test that multiple contract deployments work correctly.
BOOST_AUTO_TEST_CASE( oc_multi_contract ) { try {
   fc::temp_directory tempdir;
   constexpr bool use_genesis = true;
   validating_tester chain(tempdir, [](controller::config&){}, use_genesis);

   chain.create_accounts({"token1"_n, "token2"_n});
   chain.set_code("token1"_n, test_contracts::core_net_token_wasm());
   chain.set_abi("token1"_n, test_contracts::core_net_token_abi());
   chain.set_code("token2"_n, test_contracts::core_net_token_wasm());
   chain.set_abi("token2"_n, test_contracts::core_net_token_abi());
   chain.produce_block();

   chain.push_action("token1"_n, "create"_n, "token1"_n, mvo()
      ("issuer", "token1")
      ("maximum_supply", "1000000.0000 AAA")
   );
   chain.push_action("token2"_n, "create"_n, "token2"_n, mvo()
      ("issuer", "token2")
      ("maximum_supply", "1000000.0000 BBB")
   );
   chain.produce_block();
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
