#include <core_net/net_plugin/p2p_transport_encryption.hpp>
#include <core_net/net_plugin/protocol.hpp>

#include <fc/crypto/private_key.hpp>
#include <fc/crypto/public_key.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/io/raw.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>

using namespace core_net;

BOOST_AUTO_TEST_SUITE(p2p_transport_encryption_tests)

// ─── Node Key Tests ─────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(node_key_generate_and_load) {
   auto tmp_dir = std::filesystem::temp_directory_path() / "p2p_test_node_key";
   std::filesystem::create_directories(tmp_dir);
   auto key_file = tmp_dir / "p2p-node-key";
   std::filesystem::remove(key_file);

   // First call: generates key
   auto key1 = load_or_generate_node_key(key_file);
   BOOST_CHECK(key1.public_key.valid());
   BOOST_CHECK(key1.node_id != fc::sha256());
   BOOST_CHECK(std::filesystem::exists(key_file));

   // Verify file permissions (owner-only on unix)
#ifndef _WIN32
   auto perms = std::filesystem::status(key_file).permissions();
   BOOST_CHECK((perms & std::filesystem::perms::group_all) == std::filesystem::perms::none);
   BOOST_CHECK((perms & std::filesystem::perms::others_all) == std::filesystem::perms::none);
#endif

   // Second call: loads existing key — same identity
   auto key2 = load_or_generate_node_key(key_file);
   BOOST_CHECK(key2.node_id == key1.node_id);

   auto pk1_str = key1.public_key.to_string(fc::yield_function_t());
   auto pk2_str = key2.public_key.to_string(fc::yield_function_t());
   BOOST_CHECK_EQUAL(pk1_str, pk2_str);

   // Cleanup
   std::filesystem::remove_all(tmp_dir);
}

BOOST_AUTO_TEST_CASE(node_id_deterministic) {
   auto tmp_dir = std::filesystem::temp_directory_path() / "p2p_test_node_id";
   std::filesystem::create_directories(tmp_dir);
   auto key_file = tmp_dir / "p2p-node-key";
   std::filesystem::remove(key_file);

   auto key = load_or_generate_node_key(key_file);

   // node_id should be SHA256 of serialized public key
   auto pk_bytes = fc::raw::pack(key.public_key);
   auto expected_id = fc::sha256::hash(pk_bytes.data(), pk_bytes.size());
   BOOST_CHECK(key.node_id == expected_id);

   std::filesystem::remove_all(tmp_dir);
}

BOOST_AUTO_TEST_CASE(node_key_empty_file_rejected) {
   auto tmp_dir = std::filesystem::temp_directory_path() / "p2p_test_empty_key";
   std::filesystem::create_directories(tmp_dir);
   auto key_file = tmp_dir / "p2p-node-key";

   // Write empty file
   { std::ofstream ofs(key_file); ofs << "\n"; }

   BOOST_CHECK_THROW(load_or_generate_node_key(key_file), fc::exception);

   std::filesystem::remove_all(tmp_dir);
}

// ─── X25519 ECDH Tests ─────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(x25519_keypair_generation) {
   auto kp = generate_x25519_keypair();

   // Keys should not be all zeros
   bool priv_all_zero = true, pub_all_zero = true;
   for (auto b : kp.private_key) if (b != 0) { priv_all_zero = false; break; }
   for (auto b : kp.public_key) if (b != 0) { pub_all_zero = false; break; }
   BOOST_CHECK(!priv_all_zero);
   BOOST_CHECK(!pub_all_zero);
}

BOOST_AUTO_TEST_CASE(x25519_shared_secret_agreement) {
   auto alice = generate_x25519_keypair();
   auto bob   = generate_x25519_keypair();

   auto secret_alice = compute_x25519_shared_secret(alice.private_key, bob.public_key);
   auto secret_bob   = compute_x25519_shared_secret(bob.private_key, alice.public_key);

   BOOST_REQUIRE(secret_alice.has_value());
   BOOST_REQUIRE(secret_bob.has_value());
   BOOST_CHECK(*secret_alice == *secret_bob);
}

BOOST_AUTO_TEST_CASE(x25519_different_keys_different_secrets) {
   auto alice  = generate_x25519_keypair();
   auto bob    = generate_x25519_keypair();
   auto carol  = generate_x25519_keypair();

   auto ab = compute_x25519_shared_secret(alice.private_key, bob.public_key);
   auto ac = compute_x25519_shared_secret(alice.private_key, carol.public_key);

   BOOST_REQUIRE(ab.has_value());
   BOOST_REQUIRE(ac.has_value());
   BOOST_CHECK(*ab != *ac);
}

BOOST_AUTO_TEST_CASE(x25519_zero_public_key_rejected) {
   auto alice = generate_x25519_keypair();
   std::array<uint8_t, x25519_key_len> zero_key{};

   auto result = compute_x25519_shared_secret(alice.private_key, zero_key);
   // X25519 with the all-zero point should fail (low-order point)
   BOOST_CHECK(!result.has_value());
}

// ─── HKDF Session Key Derivation Tests ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(hkdf_session_key_deterministic) {
   auto alice = generate_x25519_keypair();
   auto bob   = generate_x25519_keypair();

   auto shared = compute_x25519_shared_secret(alice.private_key, bob.public_key);
   BOOST_REQUIRE(shared.has_value());

   fc::sha256 id_a, id_b;
   fc::rand_pseudo_bytes(id_a.data(), id_a.data_size());
   fc::rand_pseudo_bytes(id_b.data(), id_b.data_size());

   auto key1 = derive_session_key(*shared, id_a, id_b);
   auto key2 = derive_session_key(*shared, id_a, id_b);
   BOOST_CHECK(key1 == key2);
}

BOOST_AUTO_TEST_CASE(hkdf_session_key_symmetric_ids) {
   auto alice = generate_x25519_keypair();
   auto bob   = generate_x25519_keypair();

   auto shared = compute_x25519_shared_secret(alice.private_key, bob.public_key);
   BOOST_REQUIRE(shared.has_value());

   fc::sha256 id_a, id_b;
   fc::rand_pseudo_bytes(id_a.data(), id_a.data_size());
   fc::rand_pseudo_bytes(id_b.data(), id_b.data_size());

   // Both sides should derive the same key regardless of ID order
   auto key_ab = derive_session_key(*shared, id_a, id_b);
   auto key_ba = derive_session_key(*shared, id_b, id_a);
   BOOST_CHECK(key_ab == key_ba);
}

BOOST_AUTO_TEST_CASE(hkdf_different_secrets_different_keys) {
   auto kp1 = generate_x25519_keypair();
   auto kp2 = generate_x25519_keypair();
   auto kp3 = generate_x25519_keypair();

   auto shared1 = compute_x25519_shared_secret(kp1.private_key, kp2.public_key);
   auto shared2 = compute_x25519_shared_secret(kp1.private_key, kp3.public_key);
   BOOST_REQUIRE(shared1.has_value());
   BOOST_REQUIRE(shared2.has_value());

   fc::sha256 id_a, id_b;
   fc::rand_pseudo_bytes(id_a.data(), id_a.data_size());
   fc::rand_pseudo_bytes(id_b.data(), id_b.data_size());

   auto key1 = derive_session_key(*shared1, id_a, id_b);
   auto key2 = derive_session_key(*shared2, id_a, id_b);
   BOOST_CHECK(key1 != key2);
}

// ─── ChaCha20-Poly1305 AEAD Tests ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(aead_seal_open_roundtrip) {
   auto alice = generate_x25519_keypair();
   auto bob   = generate_x25519_keypair();
   auto shared = compute_x25519_shared_secret(alice.private_key, bob.public_key);
   BOOST_REQUIRE(shared.has_value());

   fc::sha256 id_a, id_b;
   fc::rand_pseudo_bytes(id_a.data(), id_a.data_size());
   fc::rand_pseudo_bytes(id_b.data(), id_b.data_size());
   auto session_key = derive_session_key(*shared, id_a, id_b);

   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(session_key, true));    // initiator
   BOOST_REQUIRE(receiver.init(session_key, false)); // responder

   const std::string plaintext = "Hello, encrypted P2P!";
   uint32_t aad = static_cast<uint32_t>(plaintext.size());

   auto encrypted = sender.seal(plaintext.data(), plaintext.size(), aad);
   BOOST_REQUIRE(encrypted.has_value());
   BOOST_CHECK(encrypted->size() == plaintext.size() + encrypted_overhead);

   auto decrypted = receiver.open(encrypted->data(), encrypted->size(), aad);
   BOOST_REQUIRE(decrypted.has_value());
   BOOST_CHECK_EQUAL(std::string(decrypted->begin(), decrypted->end()), plaintext);
}

BOOST_AUTO_TEST_CASE(aead_multiple_messages) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   for (int i = 0; i < 100; ++i) {
      std::string msg = "message #" + std::to_string(i);
      uint32_t aad = static_cast<uint32_t>(msg.size());

      auto enc = sender.seal(msg.data(), msg.size(), aad);
      BOOST_REQUIRE(enc.has_value());

      auto dec = receiver.open(enc->data(), enc->size(), aad);
      BOOST_REQUIRE(dec.has_value());
      BOOST_CHECK_EQUAL(std::string(dec->begin(), dec->end()), msg);
   }
}

BOOST_AUTO_TEST_CASE(aead_tampered_ciphertext_rejected) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   const std::string plaintext = "sensitive data";
   uint32_t aad = static_cast<uint32_t>(plaintext.size());

   auto encrypted = sender.seal(plaintext.data(), plaintext.size(), aad);
   BOOST_REQUIRE(encrypted.has_value());

   // Tamper with the ciphertext (flip a byte after the nonce)
   (*encrypted)[nonce_len + 2] ^= 0xFF;

   auto decrypted = receiver.open(encrypted->data(), encrypted->size(), aad);
   BOOST_CHECK(!decrypted.has_value());  // authentication must fail
}

BOOST_AUTO_TEST_CASE(aead_wrong_aad_rejected) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   const std::string plaintext = "test";
   uint32_t aad_send = 42;
   uint32_t aad_recv = 43;  // different AAD

   auto encrypted = sender.seal(plaintext.data(), plaintext.size(), aad_send);
   BOOST_REQUIRE(encrypted.has_value());

   auto decrypted = receiver.open(encrypted->data(), encrypted->size(), aad_recv);
   BOOST_CHECK(!decrypted.has_value());  // AAD mismatch must fail
}

BOOST_AUTO_TEST_CASE(aead_replay_rejected) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context sender, receiver;
   BOOST_REQUIRE(sender.init(key, true));
   BOOST_REQUIRE(receiver.init(key, false));

   const std::string msg1 = "first message";
   uint32_t aad1 = static_cast<uint32_t>(msg1.size());
   auto enc1 = sender.seal(msg1.data(), msg1.size(), aad1);
   BOOST_REQUIRE(enc1.has_value());

   // Decrypt first message — succeeds
   auto dec1 = receiver.open(enc1->data(), enc1->size(), aad1);
   BOOST_REQUIRE(dec1.has_value());

   // Replay the same message — must fail (nonce counter advanced)
   auto dec1_replay = receiver.open(enc1->data(), enc1->size(), aad1);
   BOOST_CHECK(!dec1_replay.has_value());
}

BOOST_AUTO_TEST_CASE(aead_bidirectional) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context alice, bob;
   BOOST_REQUIRE(alice.init(key, true));   // initiator
   BOOST_REQUIRE(bob.init(key, false));    // responder

   // Alice sends to Bob
   const std::string msg_a = "from alice";
   uint32_t aad_a = static_cast<uint32_t>(msg_a.size());
   auto enc_a = alice.seal(msg_a.data(), msg_a.size(), aad_a);
   BOOST_REQUIRE(enc_a.has_value());
   auto dec_a = bob.open(enc_a->data(), enc_a->size(), aad_a);
   BOOST_REQUIRE(dec_a.has_value());
   BOOST_CHECK_EQUAL(std::string(dec_a->begin(), dec_a->end()), msg_a);

   // Bob sends to Alice
   const std::string msg_b = "from bob";
   uint32_t aad_b = static_cast<uint32_t>(msg_b.size());
   auto enc_b = bob.seal(msg_b.data(), msg_b.size(), aad_b);
   BOOST_REQUIRE(enc_b.has_value());
   auto dec_b = alice.open(enc_b->data(), enc_b->size(), aad_b);
   BOOST_REQUIRE(dec_b.has_value());
   BOOST_CHECK_EQUAL(std::string(dec_b->begin(), dec_b->end()), msg_b);
}

BOOST_AUTO_TEST_CASE(aead_nonce_spaces_distinct) {
   std::array<uint8_t, session_key_len> key{};
   fc::rand_pseudo_bytes(reinterpret_cast<char*>(key.data()), key.size());

   aead_context ctx;
   BOOST_REQUIRE(ctx.init(key, true));

   // Initiator: seal_nonce starts at 0 (even), open_nonce starts at 1 (odd)
   BOOST_CHECK_EQUAL(ctx.get_seal_nonce(), 0u);
   BOOST_CHECK_EQUAL(ctx.get_open_nonce(), 1u);

   const std::string msg = "test";
   uint32_t aad = 4;
   auto enc = ctx.seal(msg.data(), msg.size(), aad);
   BOOST_REQUIRE(enc.has_value());

   // After seal, nonce increments by 2
   BOOST_CHECK_EQUAL(ctx.get_seal_nonce(), 2u);
   BOOST_CHECK_EQUAL(ctx.get_open_nonce(), 1u);  // unchanged
}

// ─── ECDH Signature Tests ───────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(ecdh_signature_verification) {
   auto node_key = fc::crypto::private_key::generate();
   auto node_pub = node_key.get_public_key();

   auto ecdh_kp = generate_x25519_keypair();

   // Sign the ECDH pubkey
   auto digest = compute_ecdh_sig_digest(ecdh_kp.public_key);
   auto sig = node_key.sign(digest);

   // Verify — recovered key should match node_pub
   auto recovered = fc::crypto::public_key(sig, digest, true);
   BOOST_CHECK(recovered == node_pub);
}

BOOST_AUTO_TEST_CASE(ecdh_signature_wrong_key_fails) {
   auto node_key = fc::crypto::private_key::generate();
   auto attacker_key = fc::crypto::private_key::generate();
   auto attacker_pub = attacker_key.get_public_key();

   auto ecdh_kp = generate_x25519_keypair();

   // Attacker signs with their key
   auto digest = compute_ecdh_sig_digest(ecdh_kp.public_key);
   auto sig = attacker_key.sign(digest);

   // Recovery should NOT match the legitimate node key
   auto recovered = fc::crypto::public_key(sig, digest, true);
   BOOST_CHECK(recovered != node_key.get_public_key());
   BOOST_CHECK(recovered == attacker_pub);
}

BOOST_AUTO_TEST_CASE(ecdh_signature_modified_pubkey_fails) {
   auto node_key = fc::crypto::private_key::generate();

   auto ecdh_kp = generate_x25519_keypair();

   // Sign the original ECDH pubkey
   auto digest_orig = compute_ecdh_sig_digest(ecdh_kp.public_key);
   auto sig = node_key.sign(digest_orig);

   // Modify the ECDH pubkey (MITM substitution)
   auto modified_ecdh = ecdh_kp.public_key;
   modified_ecdh[0] ^= 0xFF;

   auto digest_modified = compute_ecdh_sig_digest(modified_ecdh);

   // Verification against modified key should recover a different public key
   auto recovered = fc::crypto::public_key(sig, digest_modified, true);
   BOOST_CHECK(recovered != node_key.get_public_key());
}

// ─── Protocol Message Serialization Tests ───────────────────────────────────

BOOST_AUTO_TEST_CASE(encrypted_key_exchange_serialization) {
   encrypted_key_exchange msg;
   msg.ecdh_pubkey.fill(0x42);
   msg.family_id = "test-family";
   // sig fields left default

   auto packed = fc::raw::pack(msg);
   BOOST_CHECK(packed.size() > 0);

   encrypted_key_exchange unpacked;
   fc::datastream<const char*> ds(packed.data(), packed.size());
   fc::raw::unpack(ds, unpacked);

   BOOST_CHECK(unpacked.ecdh_pubkey == msg.ecdh_pubkey);
   BOOST_CHECK_EQUAL(unpacked.family_id, msg.family_id);
}

BOOST_AUTO_TEST_CASE(encrypted_key_exchange_in_net_message_variant) {
   encrypted_key_exchange eke;
   eke.ecdh_pubkey.fill(0xAB);

   net_message msg = eke;

   // Variant index should match
   BOOST_CHECK_EQUAL(msg.index(), static_cast<size_t>(msg_type_t::encrypted_key_exchange));

   // Round-trip through pack/unpack
   auto packed = fc::raw::pack(msg);
   net_message unpacked;
   fc::datastream<const char*> ds(packed.data(), packed.size());
   fc::raw::unpack(ds, unpacked);

   BOOST_CHECK_EQUAL(unpacked.index(), msg.index());
   auto& eke_out = std::get<encrypted_key_exchange>(unpacked);
   BOOST_CHECK(eke_out.ecdh_pubkey == eke.ecdh_pubkey);
}

BOOST_AUTO_TEST_SUITE_END()
