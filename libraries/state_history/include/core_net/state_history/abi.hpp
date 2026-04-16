#pragma once

#include <string>
#include <string_view>

namespace core_net::state_history {

std::string ship_abi_without_tables();

// Returns the SHiP session-initial ABI with version prefix matching the
// running chain's heritage: "eosio::abi/1.1" for eosio-bootstrapped chains
// (keeps legacy Antelope tooling compatible), "core_net::abi/1.1" otherwise.
// Safe to call before controller initialization — falls back to the default
// ("eosio") system_account_name until the genesis prefix is applied.
std::string_view session_wire_abi();

}