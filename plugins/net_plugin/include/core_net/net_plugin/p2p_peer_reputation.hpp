#pragma once

#include <fc/crypto/public_key.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/time.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace core_net {

// ─── Reputation Tiers ───────────────────────────────────────────────────────

enum class reputation_tier : uint8_t {
   priority   = 1,   // score > 50: full resources, priority sync
   standard   = 2,   // score 0-50: default treatment
   restricted = 3,   // score -50 to 0: reduced queue, rate limited
   block_only = 4,   // score -100 to -50: blocks only, no txn relay
   banned     = 5    // score < -100: connection dropped, auto-deny
};

inline const char* tier_name(reputation_tier t) {
   switch (t) {
      case reputation_tier::priority:   return "priority";
      case reputation_tier::standard:   return "standard";
      case reputation_tier::restricted: return "restricted";
      case reputation_tier::block_only: return "block_only";
      case reputation_tier::banned:     return "banned";
      default: return "unknown";
   }
}

inline reputation_tier score_to_tier(double score) {
   if (score > 50.0)   return reputation_tier::priority;
   if (score > 0.0)    return reputation_tier::standard;
   if (score > -50.0)  return reputation_tier::restricted;
   if (score > -100.0) return reputation_tier::block_only;
   return reputation_tier::banned;
}

// ─── Per-Peer Metrics ───────────────────────────────────────────────────────

struct peer_metrics {
   uint32_t    invalid_blocks = 0;
   uint32_t    invalid_txns = 0;
   uint32_t    connection_drops = 0;
   uint32_t    sync_failures = 0;
   uint32_t    handshake_failures = 0;
   uint64_t    unique_txns_relayed = 0;
   double      total_uptime_hours = 0.0;
   uint64_t    blocks_relayed = 0;             // total blocks received from this peer
   double      total_block_latency_ms = 0.0;   // sum of block latencies in milliseconds

   // Timestamps for decay calculation
   std::chrono::steady_clock::time_point last_invalid_block;
   std::chrono::steady_clock::time_point last_invalid_txn;
   std::chrono::steady_clock::time_point last_connection_drop;
   std::chrono::steady_clock::time_point last_sync_failure;
   std::chrono::steady_clock::time_point last_handshake_failure;
   std::chrono::steady_clock::time_point last_seen;

   /// Apply exponential decay to negative metrics based on their half-life.
   void apply_decay(std::chrono::steady_clock::time_point now) {
      auto decay = [&](uint32_t& count, std::chrono::steady_clock::time_point& last_time,
                       std::chrono::hours half_life) {
         if (count == 0 || last_time == std::chrono::steady_clock::time_point{}) return;
         auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - last_time);
         if (elapsed.count() <= 0) return;
         double factor = std::pow(0.5, static_cast<double>(elapsed.count()) / half_life.count());
         count = static_cast<uint32_t>(std::round(count * factor));
      };

      decay(invalid_blocks, last_invalid_block, std::chrono::hours(24));
      decay(invalid_txns, last_invalid_txn, std::chrono::hours(4));
      decay(connection_drops, last_connection_drop, std::chrono::hours(1));
      decay(sync_failures, last_sync_failure, std::chrono::hours(4));
      decay(handshake_failures, last_handshake_failure, std::chrono::hours(1));
   }

   /// Compute reputation score from metrics.
   double compute_score() const {
      double score = 0.0;
      score -= invalid_blocks * 100.0;
      score -= invalid_txns * 10.0;
      score -= connection_drops * 5.0;
      score -= sync_failures * 20.0;
      score -= handshake_failures * 2.0;
      score += unique_txns_relayed * 0.01;
      score += total_uptime_hours * 0.1;
      // Block relay speed bonus: lower average latency = higher bonus.
      // Peers that relay blocks within 500ms get up to +10 points.
      if (blocks_relayed > 0) {
         double avg_latency_ms = total_block_latency_ms / blocks_relayed;
         if (avg_latency_ms < 500.0 && avg_latency_ms >= 0.0) {
            score += (500.0 - avg_latency_ms) / 50.0;  // max +10 at 0ms
         }
      }
      return score;
   }
};

// ─── Ban Entry ──────────────────────────────────────────────────────────────

struct ban_entry {
   std::string                            identifier;  // key string or IP
   std::string                            type;        // "node", "family", "ip"
   std::chrono::steady_clock::time_point  until;
   std::string                            reason;
   uint32_t                               ban_count = 1; // for exponential backoff

   bool is_expired(std::chrono::steady_clock::time_point now) const {
      return now >= until;
   }

   /// Compute next ban duration using exponential backoff: 1h, 4h, 24h, 7d
   static std::chrono::hours ban_duration(uint32_t ban_count) {
      switch (ban_count) {
         case 0:
         case 1:  return std::chrono::hours(1);
         case 2:  return std::chrono::hours(4);
         case 3:  return std::chrono::hours(24);
         default: return std::chrono::hours(24 * 7);  // 7 days max
      }
   }
};

// ─── Reputation Manager ─────────────────────────────────────────────────────

/// Thread-safe peer reputation tracking system.
/// Tracks metrics at node key, family key, and IP levels.
class reputation_manager {
public:
   reputation_manager() = default;

   // ── Metric Recording ─────────────────────────────────────────────

   void record_invalid_block(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.invalid_blocks++;
      m.last_invalid_block = std::chrono::steady_clock::now();
   }

   void record_invalid_txn(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.invalid_txns++;
      m.last_invalid_txn = std::chrono::steady_clock::now();
   }

   void record_connection_drop(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.connection_drops++;
      m.last_connection_drop = std::chrono::steady_clock::now();
   }

   void record_sync_failure(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.sync_failures++;
      m.last_sync_failure = std::chrono::steady_clock::now();
   }

   void record_handshake_failure(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.handshake_failures++;
      m.last_handshake_failure = std::chrono::steady_clock::now();
   }

   void record_uptime(const std::string& node_key_str, double hours) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.total_uptime_hours += hours;
      m.last_seen = std::chrono::steady_clock::now();
   }

   void record_txn_relayed(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.unique_txns_relayed++;
   }

   void record_block_latency(const std::string& node_key_str, double latency_ms) {
      std::unique_lock lock(mtx_);
      auto& m = node_metrics_[node_key_str];
      m.blocks_relayed++;
      m.total_block_latency_ms += std::max(0.0, latency_ms);
      m.last_seen = std::chrono::steady_clock::now();
   }

   // ── Score & Tier Queries ─────────────────────────────────────────

   /// Get the reputation score for a node (with decay applied).
   double get_score(const std::string& node_key_str) {
      std::unique_lock lock(mtx_);
      auto it = node_metrics_.find(node_key_str);
      if (it == node_metrics_.end()) return 0.0;
      it->second.apply_decay(std::chrono::steady_clock::now());
      return it->second.compute_score();
   }

   /// Get the tier for a node.
   reputation_tier get_tier(const std::string& node_key_str) {
      return score_to_tier(get_score(node_key_str));
   }

   // ── Ban Management ───────────────────────────────────────────────

   /// Ban a peer by node key. Returns the ban duration.
   std::chrono::hours ban_node(const std::string& node_key_str, const std::string& reason) {
      std::unique_lock lock(mtx_);
      auto it = bans_.find(node_key_str);
      uint32_t count = 1;
      if (it != bans_.end()) count = it->second.ban_count + 1;

      auto duration = ban_entry::ban_duration(count);
      auto now = std::chrono::steady_clock::now();

      ban_entry entry;
      entry.identifier = node_key_str;
      entry.type = "node";
      entry.until = now + duration;
      entry.reason = reason;
      entry.ban_count = count;
      bans_[node_key_str] = entry;

      return duration;
   }

   /// Check if a peer is currently banned.
   bool is_banned(const std::string& identifier) {
      std::unique_lock lock(mtx_);
      auto it = bans_.find(identifier);
      if (it == bans_.end()) return false;
      if (it->second.is_expired(std::chrono::steady_clock::now())) {
         bans_.erase(it);
         return false;
      }
      return true;
   }

   /// Remove expired bans.
   void purge_expired_bans() {
      std::unique_lock lock(mtx_);
      auto now = std::chrono::steady_clock::now();
      for (auto it = bans_.begin(); it != bans_.end(); ) {
         if (it->second.is_expired(now))
            it = bans_.erase(it);
         else
            ++it;
      }
   }

   // ── Automatic threshold check ────────────────────────────────────

   /// Check if a peer should be auto-banned based on score.
   /// Returns the ban reason if auto-ban should trigger, empty string otherwise.
   std::string check_auto_ban(const std::string& node_key_str) {
      double score = get_score(node_key_str);
      if (score < -100.0) {
         return "reputation_score_below_threshold";
      }
      return {};
   }

   // ── Introspection ────────────────────────────────────────────────

   struct peer_reputation_info {
      std::string     node_key;
      double          score = 0.0;
      std::string     tier;
      uint32_t        invalid_blocks = 0;
      uint32_t        invalid_txns = 0;
      uint32_t        connection_drops = 0;
      uint32_t        sync_failures = 0;
      double          uptime_hours = 0.0;
      uint64_t        blocks_relayed = 0;
      double          avg_block_latency_ms = 0.0;
   };

   std::vector<peer_reputation_info> get_all_reputations() {
      std::unique_lock lock(mtx_);
      auto now = std::chrono::steady_clock::now();
      std::vector<peer_reputation_info> result;
      result.reserve(node_metrics_.size());
      for (auto& [key, metrics] : node_metrics_) {
         metrics.apply_decay(now);
         peer_reputation_info info;
         info.node_key = key;
         info.score = metrics.compute_score();
         info.tier = tier_name(score_to_tier(info.score));
         info.invalid_blocks = metrics.invalid_blocks;
         info.invalid_txns = metrics.invalid_txns;
         info.connection_drops = metrics.connection_drops;
         info.sync_failures = metrics.sync_failures;
         info.uptime_hours = metrics.total_uptime_hours;
         info.blocks_relayed = metrics.blocks_relayed;
         info.avg_block_latency_ms = metrics.blocks_relayed > 0
            ? metrics.total_block_latency_ms / metrics.blocks_relayed : 0.0;
         result.push_back(std::move(info));
      }
      return result;
   }

   struct ban_info {
      std::string  identifier;
      std::string  type;
      std::string  reason;
      uint32_t     ban_count = 0;
      int64_t      seconds_remaining = 0;
   };

   std::vector<ban_info> get_active_bans() {
      std::unique_lock lock(mtx_);
      auto now = std::chrono::steady_clock::now();
      std::vector<ban_info> result;
      for (auto it = bans_.begin(); it != bans_.end(); ) {
         if (it->second.is_expired(now)) {
            it = bans_.erase(it);
            continue;
         }
         ban_info bi;
         bi.identifier = it->second.identifier;
         bi.type = it->second.type;
         bi.reason = it->second.reason;
         bi.ban_count = it->second.ban_count;
         bi.seconds_remaining = std::chrono::duration_cast<std::chrono::seconds>(
            it->second.until - now).count();
         result.push_back(std::move(bi));
         ++it;
      }
      return result;
   }

   // ── Persistence ──────────────────────────────────────────────────

   /// Save reputation data to a JSON file (atomic write).
   bool save(const std::filesystem::path& file_path) {
      std::shared_lock lock(mtx_);
      try {
         fc::mutable_variant_object root;
         root("version", 1);

         // Nodes
         fc::mutable_variant_object nodes;
         auto now = std::chrono::steady_clock::now();
         for (auto& [key, metrics] : node_metrics_) {
            fc::mutable_variant_object node;
            node("score", metrics.compute_score());
            node("invalid_blocks", metrics.invalid_blocks);
            node("invalid_txns", metrics.invalid_txns);
            node("connection_drops", metrics.connection_drops);
            node("sync_failures", metrics.sync_failures);
            node("uptime_hours", metrics.total_uptime_hours);
            node("unique_txns_relayed", metrics.unique_txns_relayed);
            node("blocks_relayed", metrics.blocks_relayed);
            node("total_block_latency_ms", metrics.total_block_latency_ms);
            nodes(key, node);
         }
         root("nodes", nodes);

         // Bans
         fc::mutable_variant_object bans;
         for (auto& [key, entry] : bans_) {
            if (entry.is_expired(now)) continue;
            fc::mutable_variant_object ban;
            ban("type", entry.type);
            ban("reason", entry.reason);
            ban("ban_count", entry.ban_count);
            ban("seconds_remaining", std::chrono::duration_cast<std::chrono::seconds>(
               entry.until - now).count());
            bans(key, ban);
         }
         root("bans", bans);

         // Atomic write
         auto tmp_path = file_path;
         tmp_path += ".tmp";
         auto json_str = fc::json::to_pretty_string(fc::variant(root));
         {
            std::ofstream ofs(tmp_path, std::ios::trunc);
            if (!ofs.good()) return false;
            ofs << json_str;
            ofs.close();
            if (ofs.fail()) return false;
         }
         std::filesystem::rename(tmp_path, file_path);
         return true;
      } catch (...) {
         return false;
      }
   }

   /// Load reputation data from a JSON file.
   bool load(const std::filesystem::path& file_path) {
      if (!std::filesystem::exists(file_path)) return true; // no file = fresh start
      std::unique_lock lock(mtx_);
      try {
         auto var = fc::json::from_file(file_path);
         auto obj = var.get_object();

         if (obj.contains("nodes")) {
            auto nodes = obj["nodes"].get_object();
            for (auto it = nodes.begin(); it != nodes.end(); ++it) {
               auto node = it->value().get_object();
               auto& m = node_metrics_[it->key()];
               if (node.contains("invalid_blocks"))    m.invalid_blocks = node["invalid_blocks"].as_uint64();
               if (node.contains("invalid_txns"))      m.invalid_txns = node["invalid_txns"].as_uint64();
               if (node.contains("connection_drops"))   m.connection_drops = node["connection_drops"].as_uint64();
               if (node.contains("sync_failures"))      m.sync_failures = node["sync_failures"].as_uint64();
               if (node.contains("uptime_hours"))       m.total_uptime_hours = node["uptime_hours"].as_double();
               if (node.contains("unique_txns_relayed")) m.unique_txns_relayed = node["unique_txns_relayed"].as_uint64();
               if (node.contains("blocks_relayed"))     m.blocks_relayed = node["blocks_relayed"].as_uint64();
               if (node.contains("total_block_latency_ms")) m.total_block_latency_ms = node["total_block_latency_ms"].as_double();
               m.last_seen = std::chrono::steady_clock::now();
            }
         }

         if (obj.contains("bans")) {
            auto bans_obj = obj["bans"].get_object();
            auto now = std::chrono::steady_clock::now();
            for (auto it = bans_obj.begin(); it != bans_obj.end(); ++it) {
               auto ban = it->value().get_object();
               ban_entry entry;
               entry.identifier = it->key();
               if (ban.contains("type"))    entry.type = ban["type"].as_string();
               if (ban.contains("reason"))  entry.reason = ban["reason"].as_string();
               if (ban.contains("ban_count")) entry.ban_count = ban["ban_count"].as_uint64();
               int64_t remaining = 0;
               if (ban.contains("seconds_remaining")) remaining = ban["seconds_remaining"].as_int64();
               if (remaining > 0) {
                  entry.until = now + std::chrono::seconds(remaining);
                  bans_[it->key()] = entry;
               }
            }
         }

         return true;
      } catch (const fc::exception& e) {
         wlog("Failed to load reputation file ${f}: ${e}", ("f", file_path.string())("e", e.to_detail_string()));
         return false;
      } catch (...) {
         wlog("Failed to load reputation file ${f}", ("f", file_path.string()));
         return false;
      }
   }

private:
   mutable std::shared_mutex                     mtx_;
   std::map<std::string, peer_metrics>           node_metrics_;   // keyed by node key string
   std::map<std::string, ban_entry>              bans_;           // keyed by identifier (key or IP)
};

} // namespace core_net
