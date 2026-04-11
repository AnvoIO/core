#include <core_net/net_plugin/p2p_access_control.hpp>

#include <fc/crypto/private_key.hpp>

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/test/unit_test.hpp>

using namespace core_net;
using boost::asio::ip::address_v4;
using boost::asio::ip::address_v6;
using boost::asio::ip::make_address_v6;

// Helper: convert an IPv4 string to v6-mapped bytes
static address_v6::bytes_type v4_to_v6_bytes(const std::string& v4_str) {
   auto v4 = boost::asio::ip::make_address_v4(v4_str);
   return make_address_v6(boost::asio::ip::v4_mapped, v4).to_bytes();
}

// Helper: convert an IPv6 string to bytes
static address_v6::bytes_type v6_to_bytes(const std::string& v6_str) {
   return boost::asio::ip::make_address_v6(v6_str).to_bytes();
}

BOOST_AUTO_TEST_SUITE(p2p_access_control_tests)

// ─── CIDR Matching Tests ────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(cidr_parse_ipv4_host) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("10.0.1.5", net));
   BOOST_CHECK_EQUAL(net.prefix_len, 128);
   BOOST_CHECK(net.contains(v4_to_v6_bytes("10.0.1.5")));
   BOOST_CHECK(!net.contains(v4_to_v6_bytes("10.0.1.6")));
}

BOOST_AUTO_TEST_CASE(cidr_parse_ipv4_subnet) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("10.0.0.0/8", net));
   BOOST_CHECK_EQUAL(net.prefix_len, 96 + 8);
   BOOST_CHECK(net.contains(v4_to_v6_bytes("10.0.0.1")));
   BOOST_CHECK(net.contains(v4_to_v6_bytes("10.255.255.255")));
   BOOST_CHECK(!net.contains(v4_to_v6_bytes("11.0.0.1")));
}

BOOST_AUTO_TEST_CASE(cidr_parse_ipv4_24) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("192.168.1.0/24", net));
   BOOST_CHECK(net.contains(v4_to_v6_bytes("192.168.1.0")));
   BOOST_CHECK(net.contains(v4_to_v6_bytes("192.168.1.255")));
   BOOST_CHECK(!net.contains(v4_to_v6_bytes("192.168.2.0")));
   BOOST_CHECK(!net.contains(v4_to_v6_bytes("192.168.0.255")));
}

BOOST_AUTO_TEST_CASE(cidr_parse_ipv6_host) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("::1", net));
   BOOST_CHECK_EQUAL(net.prefix_len, 128);
   BOOST_CHECK(net.contains(v6_to_bytes("::1")));
   BOOST_CHECK(!net.contains(v6_to_bytes("::2")));
}

BOOST_AUTO_TEST_CASE(cidr_parse_ipv6_subnet) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("fe80::/10", net));
   BOOST_CHECK(net.contains(v6_to_bytes("fe80::1")));
   BOOST_CHECK(net.contains(v6_to_bytes("febf::ffff")));
   BOOST_CHECK(!net.contains(v6_to_bytes("ff00::1")));
}

BOOST_AUTO_TEST_CASE(cidr_parse_invalid) {
   cidr_network net;
   BOOST_CHECK(!cidr_network::parse("not-an-ip", net));
   BOOST_CHECK(!cidr_network::parse("", net));
   BOOST_CHECK(!cidr_network::parse("999.999.999.999", net));
}

BOOST_AUTO_TEST_CASE(cidr_slash_zero) {
   cidr_network net;
   BOOST_REQUIRE(cidr_network::parse("0.0.0.0/0", net));
   BOOST_CHECK(net.contains(v4_to_v6_bytes("1.2.3.4")));
   BOOST_CHECK(net.contains(v4_to_v6_bytes("255.255.255.255")));
}

// ─── ACL Evaluation Order Tests ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(acl_default_allow) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   auto key = fc::crypto::private_key::generate().get_public_key();
   auto ip = v4_to_v6_bytes("192.168.1.1");
   BOOST_CHECK(acl.is_allowed(key, fc::crypto::public_key(), ip));
}

BOOST_AUTO_TEST_CASE(acl_default_deny) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);

   auto key = fc::crypto::private_key::generate().get_public_key();
   auto ip = v4_to_v6_bytes("192.168.1.1");
   BOOST_CHECK(!acl.is_allowed(key, fc::crypto::public_key(), ip));
}

BOOST_AUTO_TEST_CASE(acl_deny_key_overrides_allow) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   auto key = fc::crypto::private_key::generate().get_public_key();
   acl.add_allow_key(key);
   acl.add_deny_key(key);

   auto ip = v4_to_v6_bytes("10.0.0.1");
   // deny-key is checked before allow-key
   BOOST_CHECK(!acl.is_allowed(key, fc::crypto::public_key(), ip));
}

BOOST_AUTO_TEST_CASE(acl_deny_family_overrides_allow) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   auto node_key = fc::crypto::private_key::generate().get_public_key();
   auto family_key = fc::crypto::private_key::generate().get_public_key();
   acl.add_allow_family(family_key);
   acl.add_deny_family(family_key);

   auto ip = v4_to_v6_bytes("10.0.0.1");
   BOOST_CHECK(!acl.is_allowed(node_key, family_key, ip));
}

BOOST_AUTO_TEST_CASE(acl_deny_ip_overrides_allow_key) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   auto key = fc::crypto::private_key::generate().get_public_key();
   acl.add_allow_key(key);
   acl.add_deny_ip("10.0.0.0/8");

   auto ip = v4_to_v6_bytes("10.0.0.1");
   // deny-ip checked before allow-key
   BOOST_CHECK(!acl.is_allowed(key, fc::crypto::public_key(), ip));
}

BOOST_AUTO_TEST_CASE(acl_allow_key_overrides_default_deny) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);

   auto allowed = fc::crypto::private_key::generate().get_public_key();
   auto unknown = fc::crypto::private_key::generate().get_public_key();
   acl.add_allow_key(allowed);

   auto ip = v4_to_v6_bytes("10.0.0.1");
   BOOST_CHECK(acl.is_allowed(allowed, fc::crypto::public_key(), ip));
   BOOST_CHECK(!acl.is_allowed(unknown, fc::crypto::public_key(), ip));
}

BOOST_AUTO_TEST_CASE(acl_allow_family_overrides_default_deny) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);

   auto family = fc::crypto::private_key::generate().get_public_key();
   auto node = fc::crypto::private_key::generate().get_public_key();
   acl.add_allow_family(family);

   auto ip = v4_to_v6_bytes("10.0.0.1");
   BOOST_CHECK(acl.is_allowed(node, family, ip));
}

BOOST_AUTO_TEST_CASE(acl_allow_ip_overrides_default_deny) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);
   acl.add_allow_ip("192.168.1.0/24");

   auto key = fc::crypto::private_key::generate().get_public_key();
   BOOST_CHECK(acl.is_allowed(key, fc::crypto::public_key(), v4_to_v6_bytes("192.168.1.50")));
   BOOST_CHECK(!acl.is_allowed(key, fc::crypto::public_key(), v4_to_v6_bytes("192.168.2.50")));
}

BOOST_AUTO_TEST_CASE(acl_remove_deny_key) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   auto key = fc::crypto::private_key::generate().get_public_key();
   acl.add_deny_key(key);
   BOOST_CHECK(!acl.is_allowed(key, fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));

   acl.remove_deny_key(key);
   BOOST_CHECK(acl.is_allowed(key, fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));
}

BOOST_AUTO_TEST_CASE(acl_remove_deny_ip) {
   access_control acl;
   acl.set_default_policy(access_default_policy::allow);

   acl.add_deny_ip("10.0.0.0/8");
   BOOST_CHECK(!acl.is_allowed(fc::crypto::public_key(), fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));

   acl.remove_deny_ip("10.0.0.0/8");
   BOOST_CHECK(acl.is_allowed(fc::crypto::public_key(), fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));
}

// ─── Adversarial Tests ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(acl_empty_key_not_matched) {
   // An empty/default public key should not match any allow/deny rule
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);
   acl.add_allow_key(fc::crypto::public_key()); // add empty key

   auto real_key = fc::crypto::private_key::generate().get_public_key();
   // Empty key is not valid(), so allow_key check is skipped
   BOOST_CHECK(!acl.is_allowed(real_key, fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));
}

BOOST_AUTO_TEST_CASE(acl_cidr_boundary) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);
   acl.add_allow_ip("10.0.0.128/25");  // 10.0.0.128 - 10.0.0.255

   auto key = fc::crypto::public_key();
   BOOST_CHECK(!acl.is_allowed(key, key, v4_to_v6_bytes("10.0.0.127")));  // just below
   BOOST_CHECK(acl.is_allowed(key, key, v4_to_v6_bytes("10.0.0.128")));   // first in range
   BOOST_CHECK(acl.is_allowed(key, key, v4_to_v6_bytes("10.0.0.255")));   // last in range
   BOOST_CHECK(!acl.is_allowed(key, key, v4_to_v6_bytes("10.0.1.0")));    // just above
}

BOOST_AUTO_TEST_CASE(acl_no_duplicate_rules) {
   access_control acl;
   auto key = fc::crypto::private_key::generate().get_public_key();

   acl.add_deny_key(key);
   acl.add_deny_key(key);  // duplicate
   acl.add_deny_key(key);  // duplicate

   acl.remove_deny_key(key);  // should remove all instances
   // With default allow, the key should now be allowed
   acl.set_default_policy(access_default_policy::allow);
   BOOST_CHECK(acl.is_allowed(key, fc::crypto::public_key(), v4_to_v6_bytes("10.0.0.1")));
}

BOOST_AUTO_TEST_CASE(acl_has_rules_empty) {
   access_control acl;
   BOOST_CHECK(!acl.has_rules());  // default allow, no rules
}

BOOST_AUTO_TEST_CASE(acl_has_rules_with_deny) {
   access_control acl;
   acl.add_deny_ip("10.0.0.0/8");
   BOOST_CHECK(acl.has_rules());
}

BOOST_AUTO_TEST_CASE(acl_has_rules_with_policy_change) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);
   BOOST_CHECK(acl.has_rules());
}

// ─── Rule Summary / Introspection Tests ─────────────────────────────────────

BOOST_AUTO_TEST_CASE(acl_get_rules_summary) {
   access_control acl;
   acl.set_default_policy(access_default_policy::deny);

   auto key1 = fc::crypto::private_key::generate().get_public_key();
   auto key2 = fc::crypto::private_key::generate().get_public_key();
   acl.add_deny_key(key1);
   acl.add_allow_family(key2);
   acl.add_deny_ip("10.0.0.0/8");
   acl.add_allow_ip("192.168.1.0/24");

   auto rules = acl.get_rules();
   BOOST_CHECK_EQUAL(rules.default_policy, "deny");
   BOOST_CHECK_EQUAL(rules.deny_keys.size(), 1u);
   BOOST_CHECK_EQUAL(rules.allow_families.size(), 1u);
   BOOST_CHECK_EQUAL(rules.deny_ips.size(), 1u);
   BOOST_CHECK_EQUAL(rules.allow_ips.size(), 1u);
   BOOST_CHECK_EQUAL(rules.deny_ips[0], "10.0.0.0/8");
}

// ─── Family Key Attestation Tests ───────────────────────────────────────────

BOOST_AUTO_TEST_CASE(family_sig_deterministic) {
   auto family_key = fc::crypto::private_key::generate().get_public_key();
   auto d1 = compute_family_sig_digest(family_key);
   auto d2 = compute_family_sig_digest(family_key);
   BOOST_CHECK(d1 == d2);
}

BOOST_AUTO_TEST_CASE(family_sig_different_keys) {
   auto k1 = fc::crypto::private_key::generate().get_public_key();
   auto k2 = fc::crypto::private_key::generate().get_public_key();
   BOOST_CHECK(compute_family_sig_digest(k1) != compute_family_sig_digest(k2));
}

BOOST_AUTO_TEST_CASE(family_sig_verify_roundtrip) {
   auto node_priv = fc::crypto::private_key::generate();
   auto node_pub = node_priv.get_public_key();
   auto family_key = fc::crypto::private_key::generate().get_public_key();

   auto digest = compute_family_sig_digest(family_key);
   auto sig = node_priv.sign(digest);

   auto recovered = fc::crypto::public_key(sig, digest, true);
   BOOST_CHECK(recovered == node_pub);
}

BOOST_AUTO_TEST_CASE(family_sig_wrong_signer_detected) {
   auto legitimate = fc::crypto::private_key::generate();
   auto attacker = fc::crypto::private_key::generate();
   auto family_key = fc::crypto::private_key::generate().get_public_key();

   auto digest = compute_family_sig_digest(family_key);
   auto sig = attacker.sign(digest);

   auto recovered = fc::crypto::public_key(sig, digest, true);
   BOOST_CHECK(recovered != legitimate.get_public_key());
}

BOOST_AUTO_TEST_SUITE_END()
