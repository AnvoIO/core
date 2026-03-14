#include <core_net/chain/config.hpp>
#include <core_net/chain/exceptions.hpp>

namespace core_net { namespace chain { namespace config {

namespace {
   system_accounts g_sys_accounts = system_accounts::eosio_defaults();
   std::string     g_prefix_str   = "eosio.";
   bool            g_initialized  = false;
}

// ─── system_accounts factory methods ─────────────────────────────────

system_accounts system_accounts::eosio_defaults() {
   return {
      "eosio"_n,
      "eosio.null"_n,
      "eosio.prods"_n,
      "eosio.auth"_n,
      "eosio.all"_n,
      "eosio.any"_n,
      "eosio.code"_n
   };
}

system_accounts system_accounts::core_defaults() {
   return from_prefix("core"_n);
}

system_accounts system_accounts::from_prefix(const name& prefix) {
   auto p = prefix.to_string();
   return {
      prefix,
      name(p + ".null"),
      name(p + ".prods"),
      name(p + ".auth"),
      name(p + ".all"),
      name(p + ".any"),
      name(p + ".code")
   };
}

// ─── Global state management ─────────────────────────────────────────

void set_system_accounts(const system_accounts& sa) {
   CORE_NET_ASSERT(!g_initialized, chain_exception,
                   "system accounts already initialized");
   g_sys_accounts = sa;
   g_prefix_str = sa.system_account.to_string() + ".";
   g_initialized = true;
}

// ─── Accessors ───────────────────────────────────────────────────────

const name& system_account_name()    { return g_sys_accounts.system_account; }
const name& null_account_name()      { return g_sys_accounts.null_account; }
const name& producers_account_name() { return g_sys_accounts.producers_account; }
const name& auth_scope()             { return g_sys_accounts.auth_scope; }
const name& all_scope()              { return g_sys_accounts.all_scope; }
const name& any_name()               { return g_sys_accounts.any; }
const name& code_name()              { return g_sys_accounts.code; }

const std::string& system_account_prefix_str() { return g_prefix_str; }

} } } // namespace core_net::chain::config
