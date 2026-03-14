# Genesis-Configurable System Account Names

## Context

The Anvo Network codebase has been rebranded from `eosio::` to `core_net::`, but system account names (`"eosio"`, `"eosio.token"`, etc.) remain hardcoded as static constants in `config.hpp`. The fork plan requires these to be genesis-configurable: new chains use `"core"`, `"core.token"` etc., while migrating EOSIO chains keep `"eosio"`, `"eosio.token"` etc. Same binary, different genesis config.

## Approach

**Option A from doc 08:** Replace `const static name` constants with accessor functions returning `const name&`. A global `system_accounts` struct is set once during startup from genesis. The 432+ existing references to `config::system_account_name` keep compiling — they just become function calls instead of constant reads.

## Implementation Steps

**Status:** Steps 1-5 COMPLETE, build passes 100%. Steps 6-7 remaining.

### Step 1: system_accounts struct + global accessors ✓ DONE
**Files:** `config.hpp`, new `config.cpp`, `CMakeLists.txt`

Add `system_accounts` struct with `from_prefix(name)`, `eosio_defaults()`, `core_defaults()`.
Replace 7 static constants with function declarations. Implement in `config.cpp` with a simple global (set once before threads start, no mutex needed).

Rename config constant names to neutral:
- `eosio_auth_scope` → `auth_scope()`
- `eosio_all_scope` → `all_scope()`
- `eosio_any_name` → `any_name()`
- `eosio_code_name` → `code_name()`

Keep the old names as deprecated inline wrappers initially for a two-step migration.

### Step 2: Convert all call sites (432+ references) ✓ DONE
Add `()` parentheses to all `config::system_account_name` references (becomes function call). Also update `eosio_auth_scope` → `auth_scope()`, etc. This is mechanical — the compiler catches everything.

### Step 3: Refactor SET_APP_HANDLER ✓ DONE
**File:** `controller.cpp`

Remove the macro. Replace with direct `set_apply_handler()` calls using `config::system_account_name()`. Move handler registration from constructor to a `register_native_handlers()` method called after `config::set_system_accounts()`.

Keep C++ function names as `apply_eosio_*` for now (internal, no protocol visibility). Can rename to `apply_system_*` as a follow-up.

### Step 4: Fix "eosio." prefix checks ✓ DONE
**File:** `system_contract.cpp`

Replace hardcoded `name_str.find("eosio.") != 0` with `name::prefix() != config::system_account_name()`. This cleanly handles both `"eosio."` and `"core."` chains.

### Step 5: Genesis JSON support ✓ DONE
**File:** `genesis_state.hpp`, `genesis_state.cpp`

Add `std::optional<name> system_account_prefix` — NOT in FC_REFLECT (preserves binary block log format). Custom `to_variant`/`from_variant` for JSON genesis files. Missing field defaults to `"eosio"_n`.

### Step 6: Persistence in global_property_object
**Files:** `global_property_object.hpp`, `chain_snapshot.hpp`, `controller.cpp`

Store `system_account_prefix` in `global_property_object`. Bump snapshot version (8 → 9). Legacy snapshots (v2-v8) default to `"eosio"_n`. Wire startup to read prefix from genesis/snapshot/existing-state and call `config::set_system_accounts()`.

### Step 7: Tests
**New file:** `unittests/system_accounts_tests.cpp`

- Default "eosio" chain works identically to before
- "core" prefix chain: native actions work, "core." names reserved, "eosio." names not reserved
- Snapshot round-trip preserves prefix
- Old genesis without field defaults to "eosio"

## Key Files

| File | Change |
|------|--------|
| `libraries/chain/include/core_net/chain/config.hpp` | system_accounts struct, function declarations |
| `libraries/chain/config.cpp` (NEW) | Global storage, accessors |
| `libraries/chain/CMakeLists.txt` | Add config.cpp |
| `libraries/chain/controller.cpp` | Refactor SET_APP_HANDLER, startup wiring |
| `libraries/chain/system_contract.cpp` | Configurable prefix checks |
| `libraries/chain/include/core_net/chain/genesis_state.hpp` | Optional prefix field |
| `libraries/chain/genesis_state.cpp` | Custom JSON serialization |
| `libraries/chain/include/core_net/chain/global_property_object.hpp` | Add prefix field |
| `libraries/chain/include/core_net/chain/chain_snapshot.hpp` | Bump version |
| ~432 files | Add `()` to config constant references |
| `unittests/system_accounts_tests.cpp` (NEW) | Test suite |

## Implementation plan also saved to: `_research/19_genesis_accounts_implementation.md`

## Verification

1. `make all` passes after each step
2. Existing unit tests pass with default "eosio" config (no behavioral change)
3. New tests verify "core" prefix chain behavior
4. Snapshot round-trip test with both prefix values
