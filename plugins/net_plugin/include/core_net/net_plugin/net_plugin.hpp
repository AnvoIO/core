#pragma once

#include <core_net/chain/application.hpp>
#include <core_net/net_plugin/protocol.hpp>
#include <core_net/chain_plugin/chain_plugin.hpp>
#include <core_net/producer_plugin/producer_plugin.hpp>

namespace core_net {
   using namespace appbase;

   struct connection_status {
      string            peer;
      string            remote_ip;
      string            remote_port;
      bool              connecting           = false;
      bool              syncing              = false;
      bool              is_bp_peer           = false;
      bool              is_bp_gossip_peer    = false;
      bool              is_socket_open       = false;
      bool              is_blocks_only       = false;
      bool              is_transactions_only = false;
      time_point        last_vote_received;
      handshake_message last_handshake;
   };

   struct gossip_peer {
      core_net::name               producer_name;
      std::string               server_endpoint;      // externally available address to connect to
      std::string               outbound_ip_address;  // outbound ip address for firewall
      block_timestamp_type      expiration;           // head block to remove bp_peer
   };

   class net_plugin : public appbase::plugin<net_plugin>
   {
      public:
        net_plugin();
        virtual ~net_plugin();

        APPBASE_PLUGIN_REQUIRES((chain_plugin)(producer_plugin))
        virtual void set_program_options(options_description& cli, options_description& cfg) override;
        void handle_sighup() override;

        void plugin_initialize(const variables_map& options);
        void plugin_startup();
        void plugin_shutdown();

        string                            connect( const string& endpoint );
        string                            disconnect( const string& endpoint );
        fc::variant                       status( const string& endpoint )const;
        vector<connection_status>         connections()const;
        vector<gossip_peer>               bp_gossip_peers()const;

        // Phase 2: Access control runtime API
        struct acl_key_param { chain::public_key_type key; };
        struct acl_ip_param  { string ip; };
        struct acl_rules_result {
           string                default_policy;
           vector<string>        deny_keys;
           vector<string>        deny_families;
           vector<string>        deny_ips;
           vector<string>        allow_keys;
           vector<string>        allow_families;
           vector<string>        allow_ips;
        };

        void              add_deny_key( acl_key_param params );
        void              remove_deny_key( acl_key_param params );
        void              add_deny_ip( acl_ip_param params );
        void              remove_deny_ip( acl_ip_param params );
        acl_rules_result  access_rules() const;

        // Phase 3: Reputation API
        struct peer_reputation_entry {
           string          node_key;
           double          score = 0.0;
           string          tier;
           uint32_t        invalid_blocks = 0;
           uint32_t        invalid_txns = 0;
           uint32_t        connection_drops = 0;
           uint32_t        sync_failures = 0;
           double          uptime_hours = 0.0;
           uint64_t        blocks_relayed = 0;
           double          avg_block_latency_ms = 0.0;
        };
        struct ban_entry_result {
           string          identifier;
           string          type;
           string          reason;
           uint32_t        ban_count = 0;
           int64_t         seconds_remaining = 0;
        };

        vector<peer_reputation_entry>  peer_reputation() const;
        vector<ban_entry_result>       bans() const;

        struct p2p_per_connection_metrics {
            struct connection_metric {
               uint32_t connection_id{0};
               boost::asio::ip::address_v6::bytes_type address;
               unsigned short port{0};
               bool accepting_blocks{false};
               uint32_t last_received_block{0};
               uint32_t first_available_block{0};
               uint32_t last_available_block{0};
               size_t unique_first_block_count{0};
               uint64_t latency{0};
               size_t bytes_received{0};
               std::chrono::nanoseconds last_bytes_received{0};
               size_t bytes_sent{0};
               std::chrono::nanoseconds last_bytes_sent{0};
               size_t block_sync_bytes_received{0};
               size_t block_sync_bytes_sent{0};
               bool block_sync_throttling{false};
               std::chrono::nanoseconds connection_start_time{0};
               std::string p2p_address;
               std::string unique_conn_node_id;
            };
            explicit p2p_per_connection_metrics(size_t count) {
               peers.reserve(count);
            }
            p2p_per_connection_metrics(p2p_per_connection_metrics&& other)
               : peers{std::move(other.peers)}
            {}
            p2p_per_connection_metrics(const p2p_per_connection_metrics&) = delete;
            p2p_per_connection_metrics& operator=(const p2p_per_connection_metrics&) = delete;
            std::vector<connection_metric> peers;
        };
        struct p2p_connections_metrics {
           p2p_connections_metrics(std::size_t peers, std::size_t clients, p2p_per_connection_metrics&& statistics)
              : num_peers{peers}
              , num_clients{clients}
              , stats{std::move(statistics)}
           {}
           p2p_connections_metrics(p2p_connections_metrics&& statistics)
              : num_peers{std::move(statistics.num_peers)}
              , num_clients{std::move(statistics.num_clients)}
              , stats{std::move(statistics.stats)}
           {}
           p2p_connections_metrics(const p2p_connections_metrics&) = delete;
           std::size_t num_peers   = 0;
           std::size_t num_clients = 0;
           p2p_per_connection_metrics stats;
        };

        void register_update_p2p_connection_metrics(std::function<void(p2p_connections_metrics)>&&);
        void register_increment_failed_p2p_connections(std::function<void()>&&);
        void register_increment_dropped_trxs(std::function<void()>&&);

        // for testing
        void broadcast_block(const signed_block_ptr& b, const block_id_type& id);

      private:
        std::shared_ptr<class net_plugin_impl> my;
   };

}

FC_REFLECT( core_net::connection_status, (peer)(remote_ip)(remote_port)(connecting)(syncing)
                                      (is_bp_peer)(is_bp_gossip_peer)(is_socket_open)(is_blocks_only)(is_transactions_only)
                                      (last_vote_received)(last_handshake) )
FC_REFLECT( core_net::gossip_peer, (producer_name)(server_endpoint)(outbound_ip_address)(expiration) )
FC_REFLECT( core_net::net_plugin::acl_key_param, (key) )
FC_REFLECT( core_net::net_plugin::acl_ip_param, (ip) )
FC_REFLECT( core_net::net_plugin::acl_rules_result, (default_policy)(deny_keys)(deny_families)(deny_ips)(allow_keys)(allow_families)(allow_ips) )
FC_REFLECT( core_net::net_plugin::peer_reputation_entry, (node_key)(score)(tier)(invalid_blocks)(invalid_txns)(connection_drops)(sync_failures)(uptime_hours)(blocks_relayed)(avg_block_latency_ms) )
FC_REFLECT( core_net::net_plugin::ban_entry_result, (identifier)(type)(reason)(ban_count)(seconds_remaining) )
