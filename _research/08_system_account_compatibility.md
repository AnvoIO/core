# System Account Name Compatibility Switch

## Goal

Allow new chains to use custom system account names (e.g., `core`, `core.token`)
while existing EOSIO chains migrating to this fork continue using `eosio`, `eosio.token`, etc.

This eliminates continued use of the "eosio" brand for new chains without breaking
backward compatibility for migrating chains.

## Current State: Well-Centralized

The system account names are defined in ONE place:

**File:** `libraries/chain/include/core/chain/config.hpp` (lines 22-36)

```cpp
const static name system_account_name    { "eosio"_n };
const static name null_account_name      { "eosio.null"_n };
const static name producers_account_name { "eosio.prods"_n };

const static name eosio_auth_scope       { "eosio.auth"_n };
const static name eosio_all_scope        { "eosio.all"_n };
const static name eosio_code_name        { "eosio.code"_n };
```

Most of the codebase references these through the `config::` constants, not as
hardcoded string literals. This makes the switch practical.

## Where System Account Names Are Used

### 1. Native Action Handler Registration (controller.cpp:1378-1390)

```cpp
#define SET_APP_HANDLER( receiver, contract, action) \
   set_apply_handler( account_name(#receiver), account_name(#contract), action_name(#action), \
                      &BOOST_PP_CAT(apply_, BOOST_PP_CAT(contract, BOOST_PP_CAT(_,action) ) ) )

SET_APP_HANDLER( eosio, eosio, newaccount );
SET_APP_HANDLER( eosio, eosio, setcode );
SET_APP_HANDLER( eosio, eosio, setabi );
SET_APP_HANDLER( eosio, eosio, updateauth );
SET_APP_HANDLER( eosio, eosio, deleteauth );
SET_APP_HANDLER( eosio, eosio, linkauth );
SET_APP_HANDLER( eosio, eosio, unlinkauth );
SET_APP_HANDLER( eosio, eosio, canceldelay );
```

**Problem:** This macro uses string concatenation (`#receiver`) to hardcode "eosio".
It also uses token pasting (`BOOST_PP_CAT`) to form the handler function name
(`apply_eosio_newaccount`), so the C++ function names have "eosio" baked in.

**Fix:** Replace the macro with direct `set_apply_handler` calls using the config constant:

```cpp
set_apply_handler(config::system_account_name, config::system_account_name,
                  "newaccount"_n, &apply_system_newaccount);
set_apply_handler(config::system_account_name, config::system_account_name,
                  "setcode"_n, &apply_system_setcode);
// ... etc
```

The handler functions themselves (`apply_eosio_newaccount`, etc.) can be renamed to
`apply_system_newaccount` etc. as part of the rebrand — these are internal C++ function
names with no protocol visibility.

### 2. Account Name Prefix Restriction (eosio_contract.cpp:87-90)

```cpp
if( !creator.is_privileged() ) {
    EOS_ASSERT( name_str.find( "eosio." ) != 0, action_validate_exception,
                "only privileged accounts can have names that start with 'eosio.'" );
}
```

**Fix:** Make the reserved prefix configurable:

```cpp
if( !creator.is_privileged() ) {
    const auto& prefix = config::system_account_prefix(); // "eosio." or "core."
    EOS_ASSERT( name_str.find( prefix ) != 0, action_validate_exception,
                "only privileged accounts can have names that start with '" + prefix + "'" );
}
```

### 3. Permission Name Restriction (eosio_contract.cpp)

```cpp
EOS_ASSERT( update.permission.to_string().find( "eosio." ) != 0, action_validate_exception,
            "Permission names that start with 'eosio.' are reserved" );
```

**Fix:** Same approach — use configurable prefix.

### 4. Genesis Initialization (controller.cpp:2649-2670)

```cpp
create_native_account( genesis.initial_timestamp, config::system_account_name, ... );
create_native_account( genesis.initial_timestamp, config::null_account_name, ... );
create_native_account( genesis.initial_timestamp, config::producers_account_name, ... );
```

**Already uses config constants.** Works automatically if the constants are configurable.

### 5. System ABI Assignment (controller.cpp:2582-2585)

```cpp
if( name == config::system_account_name ) {
    a.abi.assign(eosio_abi_bin, sizeof(eosio_abi_bin));
}
```

**Already uses config constant.** Works automatically.

### 6. Onblock Action (controller.cpp:4887)

```cpp
on_block_act.account = config::system_account_name;
on_block_act.authorization = vector<permission_level>{{config::system_account_name, config::active_name}};
```

**Already uses config constant.** Works automatically.

### 7. Authorization Manager (authorization_manager.cpp:307-315)

```cpp
if( scope == config::system_account_name ) {
    EOS_ASSERT( act_name != updateauth::get_name() && ... );
}
```

**Already uses config constant.** Works automatically.

### 8. OC Whitelist Check (apply_context.cpp:1087-1089)

```cpp
bool apply_context::is_eos_vm_oc_whitelisted() const {
    return receiver.prefix() == config::system_account_name || ...;
}
```

**Already uses config constant.** Works automatically.

### 9. setcode Special Case (apply_context.cpp:95-100)

```cpp
if( act->account == config::system_account_name
    && act->name == "setcode"_n
    && receiver == config::system_account_name )
```

**Already uses config constant.** Works automatically.

## Implementation Design

### Approach: Genesis-Configured System Accounts

Make system account names a genesis parameter. Once a chain boots with a specific
set of system account names, they're fixed for the life of that chain.

```cpp
// config.hpp — replace static constants with configurable values
struct system_accounts {
    name system_account_name;       // "eosio" or "core"
    name null_account_name;         // "eosio.null" or "core.null"
    name producers_account_name;    // "eosio.prods" or "core.prods"
    name auth_scope;                // "eosio.auth" or "core.auth"
    name all_scope;                 // "eosio.all" or "core.all"
    name code_name;                 // "eosio.code" or "core.code"
    std::string reserved_prefix;    // "eosio." or "core."

    // Default: EOSIO-compatible (for migrating chains)
    static system_accounts eosio_defaults() {
        return {
            "eosio"_n, "eosio.null"_n, "eosio.prods"_n,
            "eosio.auth"_n, "eosio.all"_n, "eosio.code"_n,
            "eosio."
        };
    }

    // New chain defaults
    static system_accounts new_defaults(const std::string& prefix) {
        return {
            name(prefix), name(prefix + ".null"), name(prefix + ".prods"),
            name(prefix + ".auth"), name(prefix + ".all"), name(prefix + ".code"),
            prefix + "."
        };
    }
};
```

### Genesis Configuration

```json
{
    "initial_timestamp": "2024-01-01T00:00:00.000",
    "initial_key": "EOS6MR...",
    "system_accounts": {
        "system_account": "core",
        "null_account": "core.null",
        "producers_account": "core.prods",
        "reserved_prefix": "core."
    }
}
```

For migrating EOSIO chains, omit the `system_accounts` field — defaults to `eosio.*`.

### Code Changes Required

| File | Change | Complexity |
|------|--------|-----------|
| `config.hpp` | Replace static constants with configurable struct | Low |
| `controller.cpp` | Load system accounts from genesis; refactor `SET_APP_HANDLER` macro | Medium |
| `eosio_contract.cpp` | Use configurable prefix for name restrictions | Low |
| `genesis_state.hpp` | Add optional `system_accounts` field | Low |
| `authorization_manager.cpp` | Already uses `config::` constants — no change | None |
| `apply_context.cpp` | Already uses `config::` constants — no change | None |

### Access Pattern

The `config::` constants are currently accessed as static namespace members. After
the change, they'd be accessed through the controller:

```cpp
// BEFORE
if (account == config::system_account_name) { ... }

// AFTER — option A: global accessor (simple, minimally invasive)
if (account == config::system_account_name()) { ... }

// AFTER — option B: controller accessor (cleaner, more invasive)
if (account == control.system_accounts().system_account_name) { ... }
```

**Recommendation: Option A.** Make `config::system_account_name` a function that
returns the configured value instead of a static constant. This requires changing
the least code — every existing `config::system_account_name` reference still compiles,
it just calls a function instead of reading a constant.

```cpp
// config.hpp
namespace config {
    // Set once at startup from genesis, immutable after
    void set_system_accounts(const system_accounts& sa);

    // Accessors (these replace the old static constants)
    const name& system_account_name();
    const name& null_account_name();
    const name& producers_account_name();
    const std::string& system_account_prefix();
}
```

This approach means most of the codebase doesn't change at all — the 500+ references
to `config::system_account_name` become function calls transparently.

## What About System Contracts?

System contracts (`eosio.token`, `eosio.system`, `eosio.msig`, etc.) are deployed
at genesis to whatever account names are configured. For a new chain using `core.*`:

- `core.token` gets the token contract
- `core.system` gets the system contract
- `core.msig` gets the multisig contract

The contracts themselves don't hardcode their own account name — they use
`get_self()` to determine who they are at runtime. So the same compiled WASM
works regardless of what account it's deployed to.

**One exception:** Some system contracts have cross-references. For example,
`eosio.system` might reference `eosio.token` by name for inline token transfers.
These references in the system contract source code would need to be made
configurable too (via constructor parameters or build-time configuration).
This is a system contract concern, not a node software concern.

## Consensus Safety

This change is **consensus-safe** because:

1. The system account names are set at genesis and never change
2. All nodes on the same chain use the same genesis, so they agree on the names
3. The names are deterministic — there's no runtime ambiguity
4. Migrating chains keep `eosio.*` defaults, so nothing changes for them

It does NOT require a protocol feature activation for new chains (they boot with
the configured names from block 1). For a hypothetical mid-life rename on an
existing chain, that WOULD require a protocol upgrade — but that's not the use case.

## Effort Estimate

| Task | Effort |
|------|--------|
| `config.hpp` refactor (static → configurable) | 1-2 days |
| `controller.cpp` handler registration refactor | 1 day |
| `eosio_contract.cpp` prefix configurability | Half day |
| `genesis_state.hpp` extension | Half day |
| Testing with default (eosio) config | 1 day |
| Testing with custom config | 1-2 days |
| System contract cross-references (separate repo) | 2-3 days |
| **Total** | **~1-2 weeks** |

## Summary

The system account naming is **already 90% configurable** because the codebase
consistently uses `config::` constants rather than hardcoded strings. The remaining
work is:

1. Make those constants loadable from genesis config instead of compile-time fixed
2. Refactor the `SET_APP_HANDLER` macro (the one significant hardcoded reference)
3. Make the `"eosio."` prefix restriction configurable
4. Test both modes thoroughly

New chains boot with `core.*` accounts. Migrating EOSIO chains keep `eosio.*`.
Same binary, same protocol, different genesis configuration.
