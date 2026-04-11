#pragma once

#include <fc/crypto/public_key.hpp>
#include <fc/log/logger.hpp>

#include <boost/asio/ip/address_v6.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace core_net {

// ─── CIDR Network Matching ──────────────────────────────────────────────────

/// A parsed CIDR network (IPv4 or IPv6) for efficient prefix matching.
/// IPv4 addresses are stored as v4-mapped v6 for uniform comparison.
struct cidr_network {
   boost::asio::ip::address_v6::bytes_type prefix{};
   uint8_t                                  prefix_len = 0;  // 0-128

   /// Parse from string. Accepts "1.2.3.4", "1.2.3.0/24", "::1", "fe80::/10".
   /// Returns false if the input is malformed.
   static bool parse(const std::string& s, cidr_network& out) {
      boost::system::error_code ec;

      // Try IPv4 network (CIDR notation)
      auto v4net = boost::asio::ip::make_network_v4(s, ec);
      if (!ec) {
         auto v6addr = boost::asio::ip::make_address_v6(
            boost::asio::ip::v4_mapped, v4net.network());
         out.prefix = v6addr.to_bytes();
         out.prefix_len = v4net.prefix_length() + 96;  // v4-mapped offset
         return true;
      }

      // Try plain IPv4 address (no CIDR — treat as /32)
      auto v4addr = boost::asio::ip::make_address_v4(s, ec);
      if (!ec) {
         auto v6addr = boost::asio::ip::make_address_v6(
            boost::asio::ip::v4_mapped, v4addr);
         out.prefix = v6addr.to_bytes();
         out.prefix_len = 128;
         return true;
      }

      // Try IPv6 network (CIDR notation)
      auto v6net = boost::asio::ip::make_network_v6(s, ec);
      if (!ec) {
         out.prefix = v6net.network().to_bytes();
         out.prefix_len = v6net.prefix_length();
         return true;
      }

      // Try plain IPv6 address (no CIDR — treat as /128)
      auto v6addr = boost::asio::ip::make_address_v6(s, ec);
      if (!ec) {
         out.prefix = v6addr.to_bytes();
         out.prefix_len = 128;
         return true;
      }

      return false;
   }

   /// Check if a v6-mapped IP address falls within this network.
   bool contains(const boost::asio::ip::address_v6::bytes_type& addr) const {
      if (prefix_len == 0) return true;   // /0 matches everything
      if (prefix_len > 128) return false; // invalid

      // Compare prefix_len bits
      size_t full_bytes = prefix_len / 8;
      uint8_t remaining_bits = prefix_len % 8;

      // Compare full bytes
      if (full_bytes > 0 && std::memcmp(addr.data(), prefix.data(), full_bytes) != 0) {
         return false;
      }

      // Compare remaining bits in the partial byte
      if (remaining_bits > 0) {
         uint8_t mask = static_cast<uint8_t>(0xFF << (8 - remaining_bits));
         if ((addr[full_bytes] & mask) != (prefix[full_bytes] & mask)) {
            return false;
         }
      }

      return true;
   }
};

// ─── Access Control Policy ──────────────────────────────────────────────────

enum class access_default_policy : uint8_t {
   allow = 0,
   deny  = 1
};

enum class access_decision : uint8_t {
   accept  = 0,
   reject  = 1,
   no_match = 2  // no rule matched — apply default policy
};

/// Thread-safe access control rule set.
/// Evaluation order: deny-key, deny-family, deny-ip, allow-key, allow-family, allow-ip, default.
class access_control {
public:
   access_control() = default;

   void set_default_policy(access_default_policy policy) {
      std::unique_lock lock(mtx_);
      default_policy_ = policy;
   }

   access_default_policy get_default_policy() const {
      std::shared_lock lock(mtx_);
      return default_policy_;
   }

   // ── Deny rules ───────────────────────────────────────────────────

   void add_deny_key(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      if (std::find(deny_keys_.begin(), deny_keys_.end(), key) == deny_keys_.end())
         deny_keys_.push_back(key);
   }

   void remove_deny_key(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      deny_keys_.erase(std::remove(deny_keys_.begin(), deny_keys_.end(), key), deny_keys_.end());
   }

   void add_deny_family(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      if (std::find(deny_families_.begin(), deny_families_.end(), key) == deny_families_.end())
         deny_families_.push_back(key);
   }

   void remove_deny_family(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      deny_families_.erase(std::remove(deny_families_.begin(), deny_families_.end(), key), deny_families_.end());
   }

   bool add_deny_ip(const std::string& cidr_str) {
      cidr_network net;
      if (!cidr_network::parse(cidr_str, net)) return false;
      std::unique_lock lock(mtx_);
      deny_ips_.push_back(net);
      deny_ip_strings_.push_back(cidr_str);
      return true;
   }

   bool remove_deny_ip(const std::string& cidr_str) {
      cidr_network net;
      if (!cidr_network::parse(cidr_str, net)) return false;
      std::unique_lock lock(mtx_);
      for (size_t i = 0; i < deny_ip_strings_.size(); ++i) {
         if (deny_ip_strings_[i] == cidr_str) {
            deny_ips_.erase(deny_ips_.begin() + i);
            deny_ip_strings_.erase(deny_ip_strings_.begin() + i);
            return true;
         }
      }
      return false;
   }

   // ── Allow rules ──────────────────────────────────────────────────

   void add_allow_key(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      if (std::find(allow_keys_.begin(), allow_keys_.end(), key) == allow_keys_.end())
         allow_keys_.push_back(key);
   }

   void add_allow_family(const fc::crypto::public_key& key) {
      std::unique_lock lock(mtx_);
      if (std::find(allow_families_.begin(), allow_families_.end(), key) == allow_families_.end())
         allow_families_.push_back(key);
   }

   bool add_allow_ip(const std::string& cidr_str) {
      cidr_network net;
      if (!cidr_network::parse(cidr_str, net)) return false;
      std::unique_lock lock(mtx_);
      allow_ips_.push_back(net);
      allow_ip_strings_.push_back(cidr_str);
      return true;
   }

   // ── Evaluation ───────────────────────────────────────────────────

   /// Evaluate access for a peer. Returns accept or reject.
   /// node_key: the peer's authenticated public key (from handshake)
   /// family_key: the peer's family key (from encrypted_key_exchange, empty if none)
   /// ip: the peer's remote IP address in v6-mapped bytes
   access_decision evaluate(const fc::crypto::public_key& node_key,
                             const fc::crypto::public_key& family_key,
                             const boost::asio::ip::address_v6::bytes_type& ip) const {
      std::shared_lock lock(mtx_);
      return evaluate_locked(node_key, family_key, ip);
   }

   /// Internal evaluation without locking — caller must hold mtx_.
   access_decision evaluate_locked(const fc::crypto::public_key& node_key,
                                    const fc::crypto::public_key& family_key,
                                    const boost::asio::ip::address_v6::bytes_type& ip) const {

      // 1. Check deny-key
      if (node_key.valid()) {
         if (std::find(deny_keys_.begin(), deny_keys_.end(), node_key) != deny_keys_.end())
            return access_decision::reject;
      }

      // 2. Check deny-family
      if (family_key.valid()) {
         if (std::find(deny_families_.begin(), deny_families_.end(), family_key) != deny_families_.end())
            return access_decision::reject;
      }

      // 3. Check deny-ip
      for (const auto& net : deny_ips_) {
         if (net.contains(ip)) return access_decision::reject;
      }

      // 4. Check allow-key
      if (node_key.valid()) {
         if (std::find(allow_keys_.begin(), allow_keys_.end(), node_key) != allow_keys_.end())
            return access_decision::accept;
      }

      // 5. Check allow-family
      if (family_key.valid()) {
         if (std::find(allow_families_.begin(), allow_families_.end(), family_key) != allow_families_.end())
            return access_decision::accept;
      }

      // 6. Check allow-ip
      for (const auto& net : allow_ips_) {
         if (net.contains(ip)) return access_decision::accept;
      }

      // 7. Default policy
      return access_decision::no_match;
   }

   /// Convenience: evaluate and apply default policy.
   /// Thread-safe — single lock covers both evaluation and default policy read.
   bool is_allowed(const fc::crypto::public_key& node_key,
                   const fc::crypto::public_key& family_key,
                   const boost::asio::ip::address_v6::bytes_type& ip) const {
      std::shared_lock lock(mtx_);
      auto decision = evaluate_locked(node_key, family_key, ip);
      if (decision == access_decision::accept) return true;
      if (decision == access_decision::reject) return false;
      return default_policy_ == access_default_policy::allow;
   }

   /// Check if any rules are configured (used to skip ACL when unconfigured).
   bool has_rules() const {
      std::shared_lock lock(mtx_);
      return !deny_keys_.empty() || !deny_families_.empty() || !deny_ips_.empty()
          || !allow_keys_.empty() || !allow_families_.empty() || !allow_ips_.empty()
          || default_policy_ != access_default_policy::allow;
   }

   // ── Introspection (for API endpoints) ────────────────────────────

   struct rule_summary {
      std::string                          default_policy;
      std::vector<std::string>             deny_keys;
      std::vector<std::string>             deny_families;
      std::vector<std::string>             deny_ips;
      std::vector<std::string>             allow_keys;
      std::vector<std::string>             allow_families;
      std::vector<std::string>             allow_ips;
   };

   rule_summary get_rules() const {
      std::shared_lock lock(mtx_);
      rule_summary rs;
      rs.default_policy = (default_policy_ == access_default_policy::allow) ? "allow" : "deny";
      for (const auto& k : deny_keys_)     rs.deny_keys.push_back(k.to_string(fc::yield_function_t()));
      for (const auto& k : deny_families_) rs.deny_families.push_back(k.to_string(fc::yield_function_t()));
      rs.deny_ips = deny_ip_strings_;
      for (const auto& k : allow_keys_)     rs.allow_keys.push_back(k.to_string(fc::yield_function_t()));
      for (const auto& k : allow_families_) rs.allow_families.push_back(k.to_string(fc::yield_function_t()));
      rs.allow_ips = allow_ip_strings_;
      return rs;
   }

private:
   mutable std::shared_mutex              mtx_;
   access_default_policy                  default_policy_ = access_default_policy::allow;

   std::vector<fc::crypto::public_key>    deny_keys_;
   std::vector<fc::crypto::public_key>    deny_families_;
   std::vector<cidr_network>              deny_ips_;
   std::vector<std::string>              deny_ip_strings_;  // original strings for serialization

   std::vector<fc::crypto::public_key>    allow_keys_;
   std::vector<fc::crypto::public_key>    allow_families_;
   std::vector<cidr_network>              allow_ips_;
   std::vector<std::string>              allow_ip_strings_;
};

// ─── Family Key Attestation ─────────────────────────────────────────────────

/// Compute the digest for family key attestation.
/// digest = SHA256("family:" || family_public_key_bytes)
inline fc::sha256 compute_family_sig_digest(const fc::crypto::public_key& family_key) {
   fc::sha256::encoder enc;
   static const std::string prefix = "family:";
   enc.write(prefix.data(), prefix.size());
   auto key_bytes = fc::raw::pack(family_key);
   enc.write(key_bytes.data(), key_bytes.size());
   return enc.result();
}

} // namespace core_net
