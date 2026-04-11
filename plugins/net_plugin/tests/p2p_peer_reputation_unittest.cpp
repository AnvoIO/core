#include <core_net/net_plugin/p2p_peer_reputation.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <thread>

using namespace core_net;

BOOST_AUTO_TEST_SUITE(p2p_peer_reputation_tests)

// ─── Score Computation Tests ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(initial_score_is_zero) {
   reputation_manager mgr;
   BOOST_CHECK_CLOSE(mgr.get_score("test_node"), 0.0, 0.01);
}

BOOST_AUTO_TEST_CASE(invalid_block_penalizes_heavily) {
   reputation_manager mgr;
   mgr.record_invalid_block("node_a");
   BOOST_CHECK(mgr.get_score("node_a") < -50.0);  // -100 per invalid block
}

BOOST_AUTO_TEST_CASE(invalid_txn_moderate_penalty) {
   reputation_manager mgr;
   mgr.record_invalid_txn("node_a");
   BOOST_CHECK_CLOSE(mgr.get_score("node_a"), -10.0, 0.01);
}

BOOST_AUTO_TEST_CASE(uptime_increases_score) {
   reputation_manager mgr;
   mgr.record_uptime("node_a", 100.0);  // 100 hours
   BOOST_CHECK_CLOSE(mgr.get_score("node_a"), 10.0, 0.01);  // 100 * 0.1
}

BOOST_AUTO_TEST_CASE(txn_relay_increases_score) {
   reputation_manager mgr;
   for (int i = 0; i < 10000; ++i) mgr.record_txn_relayed("node_a");
   BOOST_CHECK_CLOSE(mgr.get_score("node_a"), 100.0, 0.01);  // 10000 * 0.01
}

BOOST_AUTO_TEST_CASE(combined_score) {
   reputation_manager mgr;
   mgr.record_uptime("node_a", 500.0);    // +50
   mgr.record_invalid_txn("node_a");       // -10
   mgr.record_connection_drop("node_a");   // -5
   // Expected: 50 - 10 - 5 = 35
   BOOST_CHECK_CLOSE(mgr.get_score("node_a"), 35.0, 0.01);
}

// ─── Tier Assignment Tests ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(tier_priority) {
   BOOST_CHECK(score_to_tier(51.0) == reputation_tier::priority);
   BOOST_CHECK(score_to_tier(100.0) == reputation_tier::priority);
}

BOOST_AUTO_TEST_CASE(tier_standard) {
   BOOST_CHECK(score_to_tier(0.1) == reputation_tier::standard);
   BOOST_CHECK(score_to_tier(50.0) == reputation_tier::standard);
}

BOOST_AUTO_TEST_CASE(tier_restricted) {
   BOOST_CHECK(score_to_tier(-0.1) == reputation_tier::restricted);
   BOOST_CHECK(score_to_tier(-49.9) == reputation_tier::restricted);
}

BOOST_AUTO_TEST_CASE(tier_block_only) {
   BOOST_CHECK(score_to_tier(-50.1) == reputation_tier::block_only);
   BOOST_CHECK(score_to_tier(-99.9) == reputation_tier::block_only);
}

BOOST_AUTO_TEST_CASE(tier_banned) {
   BOOST_CHECK(score_to_tier(-100.1) == reputation_tier::banned);
   BOOST_CHECK(score_to_tier(-500.0) == reputation_tier::banned);
}

BOOST_AUTO_TEST_CASE(tier_boundary_zero) {
   // score == 0 is standard (> -50 and <= 50 is really > 0 and <= 50)
   // Actually: score > 0 → standard, score <= 0 and > -50 → restricted
   BOOST_CHECK(score_to_tier(0.0) == reputation_tier::restricted);
}

// ─── Ban Management Tests ───────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(ban_and_check) {
   reputation_manager mgr;
   auto duration = mgr.ban_node("bad_node", "test_ban");
   BOOST_CHECK(duration == std::chrono::hours(1));
   BOOST_CHECK(mgr.is_banned("bad_node"));
   BOOST_CHECK(!mgr.is_banned("good_node"));
}

BOOST_AUTO_TEST_CASE(ban_exponential_backoff) {
   reputation_manager mgr;
   auto d1 = mgr.ban_node("node_x", "first");
   BOOST_CHECK(d1 == std::chrono::hours(1));

   auto d2 = mgr.ban_node("node_x", "second");
   BOOST_CHECK(d2 == std::chrono::hours(4));

   auto d3 = mgr.ban_node("node_x", "third");
   BOOST_CHECK(d3 == std::chrono::hours(24));

   auto d4 = mgr.ban_node("node_x", "fourth");
   BOOST_CHECK(d4 == std::chrono::hours(24 * 7));

   auto d5 = mgr.ban_node("node_x", "fifth");
   BOOST_CHECK(d5 == std::chrono::hours(24 * 7));  // capped at 7 days
}

BOOST_AUTO_TEST_CASE(auto_ban_threshold) {
   reputation_manager mgr;
   // 2 invalid blocks = -200, triggers auto-ban
   mgr.record_invalid_block("node_bad");
   mgr.record_invalid_block("node_bad");
   auto reason = mgr.check_auto_ban("node_bad");
   BOOST_CHECK(!reason.empty());
   BOOST_CHECK_EQUAL(reason, "reputation_score_below_threshold");
}

BOOST_AUTO_TEST_CASE(auto_ban_not_triggered_above_threshold) {
   reputation_manager mgr;
   mgr.record_invalid_txn("node_ok");  // -10, above -100
   auto reason = mgr.check_auto_ban("node_ok");
   BOOST_CHECK(reason.empty());
}

// ─── Introspection Tests ────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(get_all_reputations) {
   reputation_manager mgr;
   mgr.record_uptime("node_a", 100.0);
   mgr.record_invalid_block("node_b");

   auto reps = mgr.get_all_reputations();
   BOOST_CHECK_EQUAL(reps.size(), 2u);

   bool found_a = false, found_b = false;
   for (const auto& r : reps) {
      if (r.node_key == "node_a") { found_a = true; BOOST_CHECK(r.score > 0); }
      if (r.node_key == "node_b") { found_b = true; BOOST_CHECK(r.score < 0); }
   }
   BOOST_CHECK(found_a && found_b);
}

BOOST_AUTO_TEST_CASE(get_active_bans) {
   reputation_manager mgr;
   mgr.ban_node("banned_1", "reason_1");
   mgr.ban_node("banned_2", "reason_2");

   auto bans = mgr.get_active_bans();
   BOOST_CHECK_EQUAL(bans.size(), 2u);
}

// ─── Persistence Tests ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(save_and_load_roundtrip) {
   auto tmp_dir = std::filesystem::temp_directory_path() / "p2p_rep_test";
   std::filesystem::create_directories(tmp_dir);
   auto file = tmp_dir / "p2p-reputation.json";
   std::filesystem::remove(file);

   {
      reputation_manager mgr;
      mgr.record_uptime("node_a", 500.0);
      mgr.record_invalid_block("node_b");
      mgr.record_invalid_txn("node_b");
      mgr.ban_node("node_c", "test_ban");

      BOOST_REQUIRE(mgr.save(file));
      BOOST_CHECK(std::filesystem::exists(file));
   }

   {
      reputation_manager mgr2;
      BOOST_REQUIRE(mgr2.load(file));

      // Verify loaded data
      auto reps = mgr2.get_all_reputations();
      BOOST_CHECK_EQUAL(reps.size(), 2u);

      bool found_a = false;
      for (const auto& r : reps) {
         if (r.node_key == "node_a") {
            found_a = true;
            BOOST_CHECK_CLOSE(r.uptime_hours, 500.0, 0.01);
         }
      }
      BOOST_CHECK(found_a);

      BOOST_CHECK(mgr2.is_banned("node_c"));
   }

   std::filesystem::remove_all(tmp_dir);
}

BOOST_AUTO_TEST_CASE(load_missing_file) {
   reputation_manager mgr;
   BOOST_CHECK(mgr.load("/nonexistent/path/reputation.json"));  // returns true (fresh start)
   BOOST_CHECK_EQUAL(mgr.get_all_reputations().size(), 0u);
}

// ─── Adversarial Tests ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(massive_negative_score) {
   reputation_manager mgr;
   for (int i = 0; i < 1000; ++i) mgr.record_invalid_block("flood_node");
   BOOST_CHECK(mgr.get_score("flood_node") < -90000.0);
   BOOST_CHECK(mgr.get_tier("flood_node") == reputation_tier::banned);
}

BOOST_AUTO_TEST_CASE(purge_expired_bans) {
   reputation_manager mgr;
   // Can't easily test time-based expiry in a unit test without mocking time,
   // but we can verify purge doesn't crash on empty/active bans
   mgr.ban_node("node_x", "test");
   mgr.purge_expired_bans();  // ban is active, should not be purged
   BOOST_CHECK(mgr.is_banned("node_x"));
}

BOOST_AUTO_TEST_SUITE_END()
