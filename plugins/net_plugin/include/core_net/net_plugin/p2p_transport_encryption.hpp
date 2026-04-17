#pragma once

#include <fc/crypto/sha256.hpp>
#include <fc/crypto/private_key.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/logger.hpp>
#include <fc/exception/exception.hpp>
#include <core_net/chain/exceptions.hpp>

#include <openssl/curve25519.h>
#include <openssl/aead.h>
#include <openssl/hkdf.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace core_net {

// ─── Constants ──────────────────────────────────────────────────────────────

static constexpr size_t   x25519_key_len      = X25519_PUBLIC_VALUE_LEN;  // 32
static constexpr size_t   x25519_private_len   = X25519_PRIVATE_KEY_LEN;  // 32
static constexpr size_t   session_key_len      = 32;                      // ChaCha20-Poly1305 key
static constexpr size_t   nonce_len            = 12;                      // 96-bit nonce
static constexpr size_t   auth_tag_len         = 16;                      // Poly1305 tag
static constexpr size_t   encrypted_overhead   = nonce_len + auth_tag_len; // 28 bytes per message

// ─── Node Key ───────────────────────────────────────────────────────────────

/// Persistent secp256k1 node identity key, stored at $data_dir/p2p-node-key.
/// The node_id is derived as SHA256(serialized_public_key), making it
/// deterministic and stable across restarts.
struct node_key_t {
   fc::crypto::private_key  private_key;
   fc::crypto::public_key   public_key;
   fc::sha256               node_id;       // SHA256(public_key)
};

/// Load or generate a persistent node key.
/// On first run, generates a secp256k1 keypair and writes the WIF private key
/// to key_file with 0600 permissions. On subsequent runs, loads from key_file.
/// Returns the node key with derived node_id.
inline node_key_t load_or_generate_node_key(const std::filesystem::path& key_file) {
   node_key_t result;

   if (std::filesystem::exists(key_file)) {
      // Load existing key
      std::ifstream ifs(key_file);
      CORE_ASSERT(ifs.good(), chain::plugin_config_exception,
                 "Unable to read node key file: ${f}", ("f", key_file.string()));

      std::string wif;
      std::getline(ifs, wif);
      ifs.close();

      // Trim whitespace
      while (!wif.empty() && (wif.back() == '\n' || wif.back() == '\r' || wif.back() == ' '))
         wif.pop_back();

      CORE_ASSERT(!wif.empty(), chain::plugin_config_exception,
                 "Node key file is empty: ${f}", ("f", key_file.string()));

      result.private_key = fc::crypto::private_key(wif);
      result.public_key = result.private_key.get_public_key();

      // Validate file permissions (unix only)
#ifndef _WIN32
      namespace fs = std::filesystem;
      auto perms = fs::status(key_file).permissions();
      if ((perms & (fs::perms::group_all | fs::perms::others_all)) != fs::perms::none) {
         wlog("Node key file ${f} has overly permissive permissions. "
              "Should be owner-read-only (0600).", ("f", key_file.string()));
      }
#endif
   } else {
      // Generate new key
      result.private_key = fc::crypto::private_key::generate();
      result.public_key  = result.private_key.get_public_key();

      // Write atomically: write to tmp file, then rename
      auto tmp_file = key_file;
      tmp_file += ".tmp";

      {
         std::ofstream ofs(tmp_file, std::ios::trunc);
         CORE_ASSERT(ofs.good(), chain::plugin_config_exception,
                    "Unable to write node key file: ${f}", ("f", tmp_file.string()));
         ofs << result.private_key.to_string(fc::yield_function_t()) << "\n";
         ofs.close();
         CORE_ASSERT(!ofs.fail(), chain::plugin_config_exception,
                    "Failed to flush node key file: ${f}", ("f", tmp_file.string()));
      }

      // Set permissions before rename so the file is never world-readable
#ifndef _WIN32
      std::filesystem::permissions(tmp_file,
         std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
         std::filesystem::perm_options::replace);
#endif

      std::filesystem::rename(tmp_file, key_file);
      ilog("P2P node identity generated: ${k}",
           ("k", result.public_key.to_string(fc::yield_function_t())));
   }

   // Derive deterministic node_id = SHA256(serialized public key)
   auto pk_bytes = fc::raw::pack(result.public_key);
   result.node_id = fc::sha256::hash(pk_bytes.data(), pk_bytes.size());

   auto pk_str = result.public_key.to_string(fc::yield_function_t());
   ilog("P2P node identity: ${k} (node_id: ${id})",
        ("k", pk_str)("id", result.node_id.str().substr(0, 16) + "..."));

   return result;
}

/// Compute the digest used to sign the ECDH public key, binding it to the
/// node's authenticated identity and preventing MITM key substitution.
/// digest = SHA256("anvo-ecdh:" || ecdh_pubkey)
inline fc::sha256 compute_ecdh_sig_digest(const std::array<uint8_t, 32>& ecdh_pubkey) {
   fc::sha256::encoder enc;
   static const std::string prefix = "anvo-ecdh:";
   enc.write(prefix.data(), prefix.size());
   enc.write(reinterpret_cast<const char*>(ecdh_pubkey.data()), ecdh_pubkey.size());
   return enc.result();
}

// ─── Ephemeral ECDH (X25519) ────────────────────────────────────────────────

struct x25519_keypair {
   std::array<uint8_t, x25519_private_len>  private_key;
   std::array<uint8_t, x25519_key_len>      public_key;
};

/// Generate an ephemeral X25519 keypair for ECDH key exchange.
inline x25519_keypair generate_x25519_keypair() {
   x25519_keypair kp;
   X25519_keypair(kp.public_key.data(), kp.private_key.data());
   return kp;
}

/// Compute the X25519 shared secret from our private key and the peer's public key.
/// Returns nullopt if the computation fails (e.g., peer sent an invalid public key).
inline std::optional<std::array<uint8_t, x25519_key_len>>
compute_x25519_shared_secret(const std::array<uint8_t, x25519_private_len>& my_private,
                              const std::array<uint8_t, x25519_key_len>& peer_public) {
   std::array<uint8_t, X25519_SHARED_KEY_LEN> shared;
   if (!X25519(shared.data(), my_private.data(), peer_public.data())) {
      return std::nullopt;  // invalid peer public key (low-order point)
   }
   return shared;
}

// ─── HKDF Session Key Derivation ────────────────────────────────────────────

/// Derive a session key from the ECDH shared secret using HKDF-SHA256.
/// The info label includes both node IDs (sorted) to prevent reflection attacks.
///
/// session_key = HKDF-SHA256(shared_secret, salt="", info="anvo-p2p-v2" || sort(id_a, id_b))
inline std::array<uint8_t, session_key_len>
derive_session_key(const std::array<uint8_t, x25519_key_len>& shared_secret,
                   const fc::sha256& my_node_id,
                   const fc::sha256& peer_node_id) {

   // Build info: "anvo-p2p-v2" || lower_node_id || higher_node_id
   static const std::string label = "anvo-p2p-v2";

   const fc::sha256& first  = (my_node_id < peer_node_id) ? my_node_id : peer_node_id;
   const fc::sha256& second = (my_node_id < peer_node_id) ? peer_node_id : my_node_id;

   std::vector<uint8_t> info;
   info.reserve(label.size() + 64);  // label + 2 x sha256
   info.insert(info.end(), label.begin(), label.end());
   info.insert(info.end(), first.data(), first.data() + first.data_size());
   info.insert(info.end(), second.data(), second.data() + second.data_size());

   std::array<uint8_t, session_key_len> key;
   int rc = HKDF(key.data(), key.size(), EVP_sha256(),
                  shared_secret.data(), shared_secret.size(),
                  nullptr, 0,           // no salt
                  info.data(), info.size());
   CORE_ASSERT(rc == 1, chain::plugin_exception, "HKDF session key derivation failed");

   return key;
}

// ─── ChaCha20-Poly1305 AEAD ────────────────────────────────────────────────

/// Per-connection encryption context. Manages the AEAD cipher state,
/// nonce counters (one per direction), and provides seal/open operations.
///
/// Thread safety: all methods must be called from the connection's strand.
class aead_context {
public:
   aead_context() = default;

   /// Initialize with the session key. Must be called once after ECDH completes.
   /// is_initiator determines which nonce counter space to use:
   ///   initiator uses even nonces (0, 2, 4, ...),  responder uses odd (1, 3, 5, ...)
   /// This prevents nonce collision when both sides send simultaneously.
   bool init(const std::array<uint8_t, session_key_len>& key, bool is_initiator) {
      const EVP_AEAD* aead = EVP_aead_chacha20_poly1305();
      EVP_AEAD_CTX_zero(&seal_ctx_);
      EVP_AEAD_CTX_zero(&open_ctx_);

      if (!EVP_AEAD_CTX_init(&seal_ctx_, aead, key.data(), key.size(),
                              EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr)) {
         return false;
      }
      if (!EVP_AEAD_CTX_init(&open_ctx_, aead, key.data(), key.size(),
                              EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr)) {
         EVP_AEAD_CTX_cleanup(&seal_ctx_);
         return false;
      }

      // Initiator sends with even nonces, responder with odd.
      // Each side receives the other's nonces, so:
      //   seal_nonce_ = initiator ? 0 : 1
      //   open_nonce_ = initiator ? 1 : 0
      seal_nonce_ = is_initiator ? 0 : 1;
      open_nonce_ = is_initiator ? 1 : 0;
      initialized_ = true;
      return true;
   }

   ~aead_context() {
      if (initialized_) {
         EVP_AEAD_CTX_cleanup(&seal_ctx_);
         EVP_AEAD_CTX_cleanup(&open_ctx_);
         // Zero key material from nonce counters (they encode no secret, but be thorough)
         OPENSSL_cleanse(&seal_nonce_, sizeof(seal_nonce_));
         OPENSSL_cleanse(&open_nonce_, sizeof(open_nonce_));
      }
   }

   aead_context(const aead_context&) = delete;
   aead_context& operator=(const aead_context&) = delete;
   aead_context(aead_context&&) = delete;
   aead_context& operator=(aead_context&&) = delete;

   bool is_initialized() const { return initialized_; }

   /// Encrypt a plaintext message. Returns the ciphertext with prepended nonce and appended auth tag.
   /// Wire format: [12-byte nonce][ciphertext][16-byte auth_tag]
   /// The message_length (from the framing header) is used as AAD.
   std::optional<std::vector<char>> seal(const char* plaintext, size_t plaintext_len,
                                          uint32_t message_length_aad) {
      if (!initialized_) return std::nullopt;

      // Build nonce from counter
      std::array<uint8_t, nonce_len> nonce{};
      encode_nonce(seal_nonce_, nonce);

      // Output: nonce || ciphertext || tag
      const size_t max_ct_len = plaintext_len + auth_tag_len;
      std::vector<char> out(nonce_len + max_ct_len);

      // Write nonce
      std::memcpy(out.data(), nonce.data(), nonce_len);

      // AAD = message_length (the 4-byte framing header, little-endian)
      const uint8_t* aad = reinterpret_cast<const uint8_t*>(&message_length_aad);

      size_t out_len = 0;
      if (!EVP_AEAD_CTX_seal(&seal_ctx_,
                              reinterpret_cast<uint8_t*>(out.data() + nonce_len), &out_len, max_ct_len,
                              nonce.data(), nonce_len,
                              reinterpret_cast<const uint8_t*>(plaintext), plaintext_len,
                              aad, sizeof(message_length_aad))) {
         return std::nullopt;
      }

      out.resize(nonce_len + out_len);
      seal_nonce_ += 2;  // increment by 2 to stay in our nonce space
      return out;
   }

   /// Decrypt a ciphertext message. Input format: [12-byte nonce][ciphertext][16-byte auth_tag]
   /// The message_length_aad is the value from the framing header (plaintext size that was advertised).
   /// Returns the decrypted plaintext, or nullopt on authentication failure.
   std::optional<std::vector<char>> open(const char* ciphertext, size_t ciphertext_len,
                                          uint32_t message_length_aad) {
      if (!initialized_) return std::nullopt;
      if (ciphertext_len < encrypted_overhead) return std::nullopt;

      // Extract nonce
      std::array<uint8_t, nonce_len> nonce{};
      std::memcpy(nonce.data(), ciphertext, nonce_len);

      // Verify nonce matches expected counter (prevents replay)
      std::array<uint8_t, nonce_len> expected_nonce{};
      encode_nonce(open_nonce_, expected_nonce);
      if (nonce != expected_nonce) {
         return std::nullopt;  // nonce mismatch — replay or out-of-order
      }

      const uint8_t* ct_data = reinterpret_cast<const uint8_t*>(ciphertext + nonce_len);
      const size_t ct_len = ciphertext_len - nonce_len;

      const uint8_t* aad = reinterpret_cast<const uint8_t*>(&message_length_aad);

      std::vector<char> plaintext(ct_len);  // at most ct_len (minus tag)
      size_t out_len = 0;
      if (!EVP_AEAD_CTX_open(&open_ctx_,
                              reinterpret_cast<uint8_t*>(plaintext.data()), &out_len, ct_len,
                              nonce.data(), nonce_len,
                              ct_data, ct_len,
                              aad, sizeof(message_length_aad))) {
         return std::nullopt;  // authentication failed
      }

      plaintext.resize(out_len);
      open_nonce_ += 2;  // increment by 2 to stay in peer's nonce space
      return plaintext;
   }

   uint64_t get_seal_nonce() const { return seal_nonce_; }
   uint64_t get_open_nonce() const { return open_nonce_; }

private:
   /// Encode a 64-bit counter into a 96-bit nonce (little-endian, zero-padded high bytes).
   static void encode_nonce(uint64_t counter, std::array<uint8_t, nonce_len>& out) {
      out.fill(0);
      std::memcpy(out.data(), &counter, sizeof(counter));  // little-endian on x86/ARM64
   }

   EVP_AEAD_CTX seal_ctx_{};
   EVP_AEAD_CTX open_ctx_{};
   uint64_t     seal_nonce_ = 0;
   uint64_t     open_nonce_ = 0;
   bool         initialized_ = false;
};

} // namespace core_net
