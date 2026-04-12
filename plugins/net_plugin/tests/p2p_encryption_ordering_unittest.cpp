// Regression tests for issue #98 — P2P encryption nonce/wire-order invariant.
//
// The bug: aead_context::open() requires nonces to arrive in strictly increasing
// counter order (seal_nonce == open_nonce on each roundtrip). The original
// implementation called seal() at enqueue-time while queued_buffer drains three
// priority queues (sync > general > trx) in a different order. Once the sync
// queue was saturated and a general/trx message was enqueued during in-flight
// writes, the wire order diverged from the seal order and the peer's open()
// failed with nonce mismatch ("Message decryption/authentication failed").
//
// These tests pin the invariant that broke (out-of-order delivery is fatal) and
// exercise the at-scale path the v0.1.2-alpha tests missed.

#include <core_net/net_plugin/p2p_transport_encryption.hpp>

#include <fc/crypto/rand.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <string>
#include <vector>

using namespace core_net;

namespace {
   std::array<uint8_t, session_key_len> random_session_key() {
      std::array<uint8_t, session_key_len> k{};
      fc::rand_pseudo_bytes(reinterpret_cast<char*>(k.data()), k.size());
      return k;
   }

   // Build a plaintext of a plausible-looking sync payload — not a real
   // signed_block, but the wire path only cares about bytes + length.
   std::vector<char> make_payload(size_t n, char fill) {
      return std::vector<char>(n, fill);
   }
}

BOOST_AUTO_TEST_SUITE(p2p_encryption_ordering_tests)

// ─── Invariant: nonces must arrive in wire order ────────────────────────────
// Documents the exact failure mode that broke in #98. If this passes, the
// receiver correctly rejects a message whose nonce is ahead of the expected
// counter — meaning any sender that assigns nonces in an order different from
// the wire order will take down the connection.
BOOST_AUTO_TEST_CASE(reordered_delivery_fails) {
   auto key = random_session_key();
   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   auto a = make_payload(128, 'A');
   auto b = make_payload(128, 'B');

   const uint32_t a_aad = static_cast<uint32_t>(a.size());
   const uint32_t b_aad = static_cast<uint32_t>(b.size());

   auto sealed_a = sender.seal(a.data(), a.size(), a_aad);  // nonce 0
   auto sealed_b = sender.seal(b.data(), b.size(), b_aad);  // nonce 2
   BOOST_REQUIRE(sealed_a.has_value());
   BOOST_REQUIRE(sealed_b.has_value());

   // Deliver B before A (what the buggy queue drain did).
   auto open_b_first = receiver.open(sealed_b->data(), sealed_b->size(), b_aad);
   BOOST_CHECK(!open_b_first.has_value());  // nonce mismatch — receiver expected 0, saw 2
}

// ─── Fix validation: in-order delivery succeeds at scale ────────────────────
// The v0.1.2-alpha unit tests only pushed 100 messages. #98 surfaced only
// after sustained sync (~10K blocks) because the write queue wasn't saturated
// enough for cross-queue reordering to kick in. This test pushes 12,000
// sequential messages to demonstrate the in-order path is sound at the
// counter magnitudes observed in production.
BOOST_AUTO_TEST_CASE(in_order_delivery_scale_12k) {
   auto key = random_session_key();
   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   constexpr int N = 12'000;
   for (int i = 0; i < N; ++i) {
      // Vary payload size to exercise different plaintext lengths — matches
      // the real mix of signed_block (large), sync_request (small), gossip.
      const size_t sz = 16 + (i * 37) % 4096;
      auto pt = make_payload(sz, static_cast<char>('a' + (i % 26)));
      const uint32_t aad = static_cast<uint32_t>(pt.size());

      auto sealed = sender.seal(pt.data(), pt.size(), aad);
      BOOST_REQUIRE_MESSAGE(sealed.has_value(), "seal failed at i=" << i);

      auto opened = receiver.open(sealed->data(), sealed->size(), aad);
      BOOST_REQUIRE_MESSAGE(opened.has_value(), "open failed at i=" << i);
      BOOST_REQUIRE_EQUAL(opened->size(), pt.size());
      BOOST_REQUIRE(std::equal(opened->begin(), opened->end(), pt.begin()));
   }

   // After 12K sends, nonces should be at 2*N in each direction's counter space.
   BOOST_CHECK_EQUAL(sender.get_seal_nonce(), static_cast<uint64_t>(2 * N));
   BOOST_CHECK_EQUAL(receiver.get_open_nonce(), static_cast<uint64_t>(2 * N));
}

// ─── Bidirectional interleave at scale ──────────────────────────────────────
// Exercises the both-sides-encrypting path. A-side seals even nonces (0,2,4,..)
// while B-side seals odd (1,3,5,..). Each direction's counter is independent.
// This is the same scenario that happens during live encrypted sync when both
// peers produce and exchange block_notice, sync_request, vote_message, etc.
BOOST_AUTO_TEST_CASE(bidirectional_interleave_scale_10k) {
   auto key = random_session_key();
   aead_context alice, bob;
   BOOST_REQUIRE(alice.init(key, true));   // initiator: sends even, receives odd
   BOOST_REQUIRE(bob.init(key, false));    // responder: sends odd, receives even

   constexpr int N = 10'000;
   for (int i = 0; i < N; ++i) {
      // Alice → Bob
      auto a_pt = make_payload(64 + (i % 256), 'A');
      const uint32_t a_aad = static_cast<uint32_t>(a_pt.size());
      auto a_sealed = alice.seal(a_pt.data(), a_pt.size(), a_aad);
      BOOST_REQUIRE(a_sealed.has_value());
      auto a_opened = bob.open(a_sealed->data(), a_sealed->size(), a_aad);
      BOOST_REQUIRE_MESSAGE(a_opened.has_value(), "A→B open failed at i=" << i);

      // Bob → Alice
      auto b_pt = make_payload(48 + (i % 192), 'B');
      const uint32_t b_aad = static_cast<uint32_t>(b_pt.size());
      auto b_sealed = bob.seal(b_pt.data(), b_pt.size(), b_aad);
      BOOST_REQUIRE(b_sealed.has_value());
      auto b_opened = alice.open(b_sealed->data(), b_sealed->size(), b_aad);
      BOOST_REQUIRE_MESSAGE(b_opened.has_value(), "B→A open failed at i=" << i);
   }

   // Alice sent N even-nonce messages: counter advanced to 2*N.
   // Bob sent N odd-nonce messages: counter advanced from 1 to 1 + 2*N.
   BOOST_CHECK_EQUAL(alice.get_seal_nonce(), static_cast<uint64_t>(2 * N));
   BOOST_CHECK_EQUAL(bob.get_seal_nonce(), static_cast<uint64_t>(1 + 2 * N));
}

BOOST_AUTO_TEST_SUITE_END()
