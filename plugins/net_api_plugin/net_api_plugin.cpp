#include <core_net/net_api_plugin/net_api_plugin.hpp>
#include <core_net/chain/exceptions.hpp>
#include <core_net/chain/transaction.hpp>

#include <fc/variant.hpp>
#include <fc/io/json.hpp>

#include <chrono>

namespace core_net { namespace detail {
  struct net_api_plugin_empty {};
}}

FC_REFLECT(core_net::detail::net_api_plugin_empty, );

namespace core_net {

   static auto _net_api_plugin = application::register_plugin<net_api_plugin>();

using namespace core_net;

#define CALL_WITH_400(api_name, category, api_handle, call_name, INVOKE, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   api_category::category, \
   [&api_handle](string&&, string&& body, url_response_callback&& cb) mutable { \
          try { \
             INVOKE \
             cb(http_response_code, fc::variant(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

#define INVOKE_R_R(api_handle, call_name, in_param) \
     auto params = parse_params<in_param, http_params_types::params_required>(body);\
     fc::variant result( api_handle.call_name( std::move(params) ) );

#define INVOKE_R_V(api_handle, call_name) \
     body = parse_params<std::string, http_params_types::no_params>(body); \
     auto result = api_handle.call_name();

#define INVOKE_V_R(api_handle, call_name, in_param) \
     auto params = parse_params<in_param, http_params_types::params_required>(body);\
     api_handle.call_name( std::move(params) ); \
     core_net::detail::net_api_plugin_empty result;

#define INVOKE_V_V(api_handle, call_name) \
     body = parse_params<std::string, http_params_types::no_params>(body); \
     api_handle.call_name(); \
     core_net::detail::net_api_plugin_empty result;


void net_api_plugin::plugin_startup() {
   dlog("starting net_api_plugin");
   // lifetime of plugin is lifetime of application
   auto& net_mgr = app().get_plugin<net_plugin>();
   app().get_plugin<http_plugin>().add_async_api({
       CALL_WITH_400(net, net_rw, net_mgr, connect,
            INVOKE_R_R(net_mgr, connect, std::string), 201),
       CALL_WITH_400(net, net_rw, net_mgr, disconnect,
            INVOKE_R_R(net_mgr, disconnect, std::string), 201),
       CALL_WITH_400(net, net_ro, net_mgr, status,
            INVOKE_R_R(net_mgr, status, std::string), 201),
       CALL_WITH_400(net, net_ro, net_mgr, connections,
            INVOKE_R_V(net_mgr, connections), 201),
       CALL_WITH_400(net, net_ro, net_mgr, bp_gossip_peers,
            INVOKE_R_V(net_mgr, bp_gossip_peers), 201),
       CALL_WITH_400(net, net_rw, net_mgr, add_deny_key,
            INVOKE_V_R(net_mgr, add_deny_key, net_plugin::acl_key_param), 201),
       CALL_WITH_400(net, net_rw, net_mgr, remove_deny_key,
            INVOKE_V_R(net_mgr, remove_deny_key, net_plugin::acl_key_param), 201),
       CALL_WITH_400(net, net_rw, net_mgr, add_deny_ip,
            INVOKE_V_R(net_mgr, add_deny_ip, net_plugin::acl_ip_param), 201),
       CALL_WITH_400(net, net_rw, net_mgr, remove_deny_ip,
            INVOKE_V_R(net_mgr, remove_deny_ip, net_plugin::acl_ip_param), 201),
       CALL_WITH_400(net, net_ro, net_mgr, access_rules,
            INVOKE_R_V(net_mgr, access_rules), 201),
       CALL_WITH_400(net, net_ro, net_mgr, peer_reputation,
            INVOKE_R_V(net_mgr, peer_reputation), 201),
       CALL_WITH_400(net, net_ro, net_mgr, bans,
            INVOKE_R_V(net_mgr, bans), 201),
  } );
}

void net_api_plugin::plugin_initialize(const variables_map& options) {
   try {
      const auto& _http_plugin = app().get_plugin<http_plugin>();
      if( !_http_plugin.is_on_loopback(api_category::net_rw)) {
         wlog( "\n"
               "**********SECURITY WARNING**********\n"
               "*                                  *\n"
               "* --        Net RW API          -- *\n"
               "* - EXPOSED to the LOCAL NETWORK - *\n"
               "* - USE ONLY ON SECURE NETWORKS! - *\n"
               "*                                  *\n"
               "************************************\n" );
      }
   } FC_LOG_AND_RETHROW()
}


#undef INVOKE_R_R
#undef INVOKE_R_V
#undef INVOKE_V_R
#undef INVOKE_V_V
#undef CALL

}
