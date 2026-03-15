#include <core_net/testing/tester.hpp>
#include <test_contracts.hpp>
#include <boost/test/unit_test.hpp>

using namespace core_net;
using namespace core_net::chain;
using namespace core_net::testing;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(corevmoc_limits_tests)

// common routine to verify wasm_execution_error is raised when a resource
// limit specified in corevmoc_config is reached
// eosio.* is whitelisted, use a different account to avoid whitelist
void limit_violated_test(const corevmoc::config& corevmoc_config, const std::string& account, bool expect_exception) {
   fc::temp_directory tempdir;

   constexpr bool use_genesis = true;
   validating_tester chain(
      tempdir,
      [&](controller::config& cfg) {
         cfg.corevmoc_config = corevmoc_config;
      },
      use_genesis
   );

   name acc = name{account};

   chain.create_accounts({acc});
   chain.set_code(acc, test_contracts::core_net_token_wasm());
   chain.set_abi(acc, test_contracts::core_net_token_abi());

#ifdef CORE_NET_VM_OC_RUNTIME_ENABLED
   if (chain.control->is_core_vm_oc_enabled()) {
      if (expect_exception) {
         BOOST_CHECK_EXCEPTION(
            chain.push_action( acc, "create"_n, acc, mvo()
               ( "issuer", account )
               ( "maximum_supply", "1000000.00 TOK" )),
            core_net::chain::wasm_execution_error,
            [](const core_net::chain::wasm_execution_error& e) {
               return expect_assert_message(e, "failed to compile wasm");
            }
         );
      } else {
         chain.push_action( acc, "create"_n, acc, mvo()
            ( "issuer", account )
            ( "maximum_supply", "1000000.00 TOK" ));
      }
   } else
#endif
   {
      chain.push_action( acc, "create"_n, acc, mvo()
         ( "issuer", account )
         ( "maximum_supply", "1000000.00 TOK" )
      );
   }
}

// common routine to verify no wasm_execution_error is raised
// because limits specified in corevmoc_config are not reached
void limit_not_violated_test(const corevmoc::config& corevmoc_config) {
   fc::temp_directory tempdir;

   constexpr bool use_genesis = true;
   validating_tester chain(
      tempdir,
      [&](controller::config& cfg) {
         cfg.corevmoc_config = corevmoc_config;
      },
      use_genesis
   );

   chain.create_accounts({"eosio.token"_n});
   chain.set_code("eosio.token"_n, test_contracts::core_net_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::core_net_token_abi());

   chain.push_action( "eosio.token"_n, "create"_n, "eosio.token"_n, mvo()
      ( "issuer", "eosio.token" )
      ( "maximum_supply", "1000000.00 TOK" )
   );
}

static corevmoc::config make_corevmoc_config_without_limits() {
   corevmoc::config cfg;
   cfg.non_whitelisted_limits.cpu_limit.reset();
   cfg.non_whitelisted_limits.vm_limit.reset();
   cfg.non_whitelisted_limits.stack_size_limit.reset();
   cfg.non_whitelisted_limits.generated_code_size_limit.reset();
   return cfg;
}

// test all limits are not set for tests
BOOST_AUTO_TEST_CASE( limits_not_set ) { try {
   validating_tester chain;
   auto& cfg = chain.get_config();

   BOOST_REQUIRE(cfg.corevmoc_config.non_whitelisted_limits.cpu_limit == std::nullopt);
   BOOST_REQUIRE(cfg.corevmoc_config.non_whitelisted_limits.vm_limit == std::nullopt);
   BOOST_REQUIRE(cfg.corevmoc_config.non_whitelisted_limits.stack_size_limit == std::nullopt);
   BOOST_REQUIRE(cfg.corevmoc_config.non_whitelisted_limits.generated_code_size_limit == std::nullopt);
} FC_LOG_AND_RETHROW() }

// test limits are not enforced unless limits in corevmoc_config
// are modified
BOOST_AUTO_TEST_CASE( limits_not_enforced ) { try {
   corevmoc::config corevmoc_config = make_corevmoc_config_without_limits();
   limit_not_violated_test(corevmoc_config);
} FC_LOG_AND_RETHROW() }

// UBSAN & ASAN can add massive virtual memory usage; skip this test when either are enabled
#if !__has_feature(undefined_behavior_sanitizer) && !__has_feature(address_sanitizer)
// test VM limit are checked
BOOST_AUTO_TEST_CASE( vm_limit ) { try {
   corevmoc::config corevmoc_config = make_corevmoc_config_without_limits();

   // set vm_limit to a small value such that it is exceeded
   corevmoc_config.non_whitelisted_limits.vm_limit = 64u*1024u*1024u;
   limit_violated_test(corevmoc_config, "test", true);
   limit_violated_test(corevmoc_config, "eosio.token", false); // whitelisted account, no exception

   // set vm_limit to a large value such that it is not exceeded
   corevmoc_config.non_whitelisted_limits.vm_limit = 128u*1024u*1024u;
   limit_not_violated_test(corevmoc_config);
} FC_LOG_AND_RETHROW() }

//make sure vm_limit is populated for a default constructed config (what core_netd will use)
BOOST_AUTO_TEST_CASE( check_config_default_vm_limit ) { try {
   corevmoc::config corevmoc_config;

   BOOST_REQUIRE(corevmoc_config.non_whitelisted_limits.vm_limit);
} FC_LOG_AND_RETHROW() }
#endif

// test stack size limit is checked
BOOST_AUTO_TEST_CASE( stack_limit ) { try {
   corevmoc::config corevmoc_config = make_corevmoc_config_without_limits();

   // The stack size of the compiled WASM in the test is 104.
   // Set stack_size_limit one less than the actual needed stack size
   corevmoc_config.non_whitelisted_limits.stack_size_limit = 103;
   limit_violated_test(corevmoc_config, "test", true);
   limit_violated_test(corevmoc_config, "eosio.token", false); // whitelisted account, no exception

   // set stack_size_limit to the actual needed stack size
   corevmoc_config.non_whitelisted_limits.stack_size_limit = 104;
   limit_not_violated_test(corevmoc_config);
} FC_LOG_AND_RETHROW() }

// test generated code size limit is checked
BOOST_AUTO_TEST_CASE( generated_code_size_limit ) { try {
   corevmoc::config corevmoc_config = make_corevmoc_config_without_limits();

   // Generated code size can vary based on the version of LLVM in use. Since this test
   // isn't intended to detect minute differences or regressions, give the range a wide
   // berth to work on. As a single data point, LLVM11 used in reproducible builds during
   // Spring 1.0 timeframe was 36856
   corevmoc_config.non_whitelisted_limits.generated_code_size_limit = 20*1024;
   limit_violated_test(corevmoc_config, "test", true);
   limit_violated_test(corevmoc_config, "eosio.token", false); // whitelisted account, no exception

   corevmoc_config.non_whitelisted_limits.generated_code_size_limit = 40*1024;
   limit_not_violated_test(corevmoc_config);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
