/**
 * Protocol feature backwards compatibility test.
 *
 * Verifies that feature_digests for all upstream Leap/Spring protocol features
 * match their known on-chain values. These digests are consensus-critical and
 * immutable — any chain that has activated a feature has its digest permanently
 * recorded in block state. If this test fails, it means a description text or
 * dependency was accidentally modified, breaking compatibility with existing
 * Antelope chains.
 *
 * The existing gen_protocol_feature_digest_tests.py only checks internal
 * consistency (description text matches its own hardcoded hash). This test
 * catches the case where both are changed together.
 */
#include <core_net/chain/protocol_feature_manager.hpp>
#include <boost/test/unit_test.hpp>
#include <map>

using namespace core_net::chain;

namespace {
   // Build the protocol feature set and compute all feature_digests,
   // using the same code path as initialize_protocol_features().
   std::map<builtin_protocol_feature_t, digest_type> compute_all_feature_digests() {
      protocol_feature_set pfs;
      std::map<builtin_protocol_feature_t, digest_type> computed;

      std::function<digest_type(builtin_protocol_feature_t)> add_feature =
         [&](builtin_protocol_feature_t codename) -> digest_type {
            auto it = computed.find(codename);
            if (it != computed.end()) return it->second;

            auto f = protocol_feature_set::make_default_builtin_protocol_feature(
               codename, [&](builtin_protocol_feature_t dep) { return add_feature(dep); });
            const auto& pf = pfs.add_feature(f);
            computed[codename] = pf.feature_digest;
            return pf.feature_digest;
         };

      for (const auto& [codename, spec] : builtin_protocol_feature_codenames) {
         add_feature(codename);
      }
      return computed;
   }
}

BOOST_AUTO_TEST_SUITE(protocol_feature_compat_tests)

// Upstream Leap/Spring feature_digests that are already activated on existing chains.
// These values are immutable consensus constants — never modify them.
BOOST_AUTO_TEST_CASE(upstream_feature_digests_unchanged) {
   auto computed = compute_all_feature_digests();

   // Hardcoded feature_digests from upstream AntelopeIO/spring at our fork point.
   // Cross-referenced with tutorials/bios-boot-tutorial/bios-boot-tutorial.py.
   std::map<builtin_protocol_feature_t, std::string> expected;
   expected[builtin_protocol_feature_t::preactivate_feature]              = "0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd";
   expected[builtin_protocol_feature_t::only_link_to_existing_permission] = "1a99a59d87e06e09ec5b028a9cbb7749b4a5ad8819004365d02dc4379a8b7241";
   expected[builtin_protocol_feature_t::replace_deferred]                 = "ef43112c6543b88db2283a2e077278c315ae2c84719a8b25f25cc88565fbea99";
   expected[builtin_protocol_feature_t::no_duplicate_deferred_id]         = "4a90c00d55454dc5b059055ca213579c6ea856967712a56017487886a4d4cc0f";
   expected[builtin_protocol_feature_t::fix_linkauth_restriction]         = "e0fb64b1085cc5538970158d05a009c24e276fb94e1a0bf6a528b48fbc4ff526";
   expected[builtin_protocol_feature_t::disallow_empty_producer_schedule] = "68dcaa34c0517d19666e6b33add67351d8c5f69e999ca1e37931bc410a297428";
   expected[builtin_protocol_feature_t::restrict_action_to_self]          = "ad9e3d8f650687709fd68f4b90b41f7d825a365b02c23a636cef88ac2ac00c43";
   expected[builtin_protocol_feature_t::only_bill_first_authorizer]       = "8ba52fe7a3956c5cd3a656a3174b931d3bb2abb45578befc59f283ecd816a405";
   expected[builtin_protocol_feature_t::forward_setcode]                  = "2652f5f96006294109b3dd0bbde63693f55324af452b799ee137a81a905eed25";
   expected[builtin_protocol_feature_t::get_sender]                       = "f0af56d2c5a48d60a4a5b5c903edfb7db3a736a94ed589d0b797df33ff9d3e1d";
   expected[builtin_protocol_feature_t::ram_restrictions]                 = "4e7bf348da00a945489b2a681749eb56f5de00b900014e137ddae39f48f69d67";
   expected[builtin_protocol_feature_t::webauthn_key]                     = "4fca8bd82bbd181e714e283f83e1b45d95ca5af40fb89ad3977b653c448f78c2";
   expected[builtin_protocol_feature_t::wtmsig_block_signatures]          = "299dcb6af692324b899b39f16d5a530a33062804e41f09dc97e9f156b4476707";
   expected[builtin_protocol_feature_t::action_return_value]              = "c3a6138c5061cf291310887c0b5c71fcaffeab90d5deb50d3b9e687cead45071";
   expected[builtin_protocol_feature_t::configurable_wasm_limits]         = "d528b9f6e9693f45ed277af93474fd473ce7d831dae2180cca35d907bd10cb40";
   expected[builtin_protocol_feature_t::blockchain_parameters]            = "5443fcf88330c586bc0e5f3dee10e7f63c76c00249c87fe4fbf7f38c082006b4";
   expected[builtin_protocol_feature_t::get_code_hash]                    = "bcd2a26394b36614fd4894241d3c451ab0f6fd110958c3423073621a70826e99";
   expected[builtin_protocol_feature_t::crypto_primitives]                = "6bcb40a24e49c26d0a60513b6aeb8551d264e4717f306b81a37a5afb3b47cedc";
   expected[builtin_protocol_feature_t::get_block_num]                    = "35c2186cc36f7bb4aeaf4487b36e57039ccf45a9136aa856a5d569ecca55ef2b";
   expected[builtin_protocol_feature_t::bls_primitives]                   = "63320dd4a58212e4d32d1f58926b73ca33a247326c2a5e9fd39268d2384e011a";
   expected[builtin_protocol_feature_t::disable_deferred_trxs_stage_1]    = "fce57d2331667353a0eac6b4209b67b843a7262a848af0a49a6e2fa9f6584eb4";
   expected[builtin_protocol_feature_t::disable_deferred_trxs_stage_2]    = "09e86cb0accf8d81c9e85d34bea4b925ae936626d00c984e4691186891f5bc16";
   expected[builtin_protocol_feature_t::savanna]                          = "cbe0fafc8fcc6cc998395e9b6de6ebd94644467b1b4a97ec126005df07013c52";

   for (const auto& [codename, expected_digest] : expected) {
      auto it = computed.find(codename);
      BOOST_REQUIRE_MESSAGE(it != computed.end(),
         "missing computed digest for feature " << static_cast<uint32_t>(codename));
      BOOST_CHECK_MESSAGE(it->second.str() == expected_digest,
         builtin_protocol_feature_codename(codename)
         << ": expected " << expected_digest
         << " but got " << it->second.str()
         << " — upstream protocol feature digest changed, this breaks chain compatibility");
   }
}

// Core-namespaced feature_digests for new Core chains.
// These are NOT activated on any existing chain yet, but once a Core chain
// activates them they become immutable too.
BOOST_AUTO_TEST_CASE(core_feature_digests_unchanged) {
   auto computed = compute_all_feature_digests();

   std::map<builtin_protocol_feature_t, std::string> expected;
   expected[builtin_protocol_feature_t::core_fix_linkauth_restriction] = "bcbcc16b8a06215e7aa07126ab3e723725c622801f3e010b2e43cedd1888b00e";
   expected[builtin_protocol_feature_t::core_forward_setcode]          = "8c4a6f769e8ff053bfad8b0bc4f0dd240fce3d1b98c7a8ed9eed57f755b6e4e1";
   expected[builtin_protocol_feature_t::core_consensus_v2]             = "e44f086e7934483336755945165dab4b5a895971562f2ebb4b2c858bec4cfd52";

   for (const auto& [codename, expected_digest] : expected) {
      auto it = computed.find(codename);
      BOOST_REQUIRE_MESSAGE(it != computed.end(),
         "missing computed digest for Core feature " << static_cast<uint32_t>(codename));
      BOOST_CHECK_MESSAGE(it->second.str() == expected_digest,
         builtin_protocol_feature_codename(codename)
         << ": expected " << expected_digest
         << " but got " << it->second.str()
         << " — Core protocol feature digest changed");
   }
}

BOOST_AUTO_TEST_SUITE_END()
