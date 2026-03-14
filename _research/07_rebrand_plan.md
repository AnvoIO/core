# Rebrand Plan: Removing EOSIO/Antelope/Spring/Leap Branding

## Scope

Remove all references to:
- **eosio** / **EOSIO** / **EOS** (original project)
- **antelope** / **AntelopeIO** / **Antelope** (successor project)
- **spring** / **Spring** (current project name)
- **leap** / **Leap** (predecessor to Spring)
- **block.one** / **Block.one** (original company)
- **nodeos** / **cleos** / **keosd** (executable names)

Replace with the new project identity: **Anvo Network** (`core_net::` namespace,
`CORE_NET_` macro prefix, `core.*` system accounts,
`core_netd`/`core-cli`/`core-wallet`/`core-util` executables).

## Naming Convention (Finalized)

| Layer | Old | New |
|-------|-----|-----|
| C++ namespace | `eosio::` | `core_net::` |
| C++ identifier prefix | `eosio_` | `core_net_` |
| Macro prefix | `EOSIO_` | `CORE_NET_` |
| Include paths | `#include <eosio/...>` | `#include <core_net/...>` |
| Node daemon | `nodeos` | `core_netd` |
| CLI | `cleos` | `core-cli` |
| Wallet | `keosd` | `core-wallet` |
| Utility | `spring-util` | `core-util` |
| CMake project | `antelope-spring` | `anvo-core` |
| CMake targets | `eosio_chain` | `core_net_chain` |
| keosd_ identifiers | `keosd_*`, `_keosd_*` | `core_wallet_*`, `_core_wallet_*` |
| WASM intrinsics | `eosio_assert`, `eosio_exit` | `core_net_assert`, `core_net_exit` |
| WASM injection | `eosio_injection.*` | `core_net_injection.*` |
| System accounts (new) | — | `core`, `core.token`, `core.bios`, etc. |
| System accounts (compat) | `eosio`, `eosio.token` | Kept via genesis config switch |

**Why `core_net::` instead of `core::`:** `core::` collides with `boost::core::`.
`core_net::` is unique and provides a consistent prefix across namespace (`core_net::`),
identifiers (`core_net_*`), and macros (`CORE_NET_*`).

## Scale of the Effort

### Total Counts by Term

| Term | Occurrences | Files | Directories |
|------|------------|-------|-------------|
| **eosio** (all forms) | ~7,693 | ~500+ | 27 |
| **nodeos** | ~1,939 | ~70 | 2 |
| **cleos** | ~560 | ~40 | 2 |
| **spring** | ~314 | ~30 | 2 |
| **antelope** | ~271 | ~30 | 0 |
| **keosd** | ~186 | ~20 | 2 |
| **eos_vm / EOS_VM** | ~70 | ~15 | 0 |
| **leap** | ~44 | ~10 | 0 |
| **block.one** | ~2 | 2 | 0 |
| **TOTAL** | **~11,079** | | **35** |

### What Does NOT Need Renaming

- **`fc::` namespace** (~3,200 occurrences) — generic foundation library, not branded
- **WASM bytecode** — compiled contracts don't contain namespace strings
- **Submodule internals that are kept as submodules** — boost, boringssl, prometheus,
  rapidjson, secp256k1 (no eosio refs, upstream sources)

## Prerequisites: Submodule Absorption

**Must be done before the rename.** Absorbing submodules with `eosio` references
eliminates cross-boundary issues that made the first rename attempt (on `rebrand/core`)
messy.

| Submodule | Action | Reason |
|-----------|--------|--------|
| `libraries/eos-vm` | **Absorbed** | 158 files with `eosio::vm::` → `core_net::vm::` |
| `libraries/appbase` | **Absorbed** | Eliminate AntelopeIO dependency |
| `libraries/softfloat` | **Absorbed** | Eliminate AntelopeIO dependency |
| `libraries/cli11/cli11` | **Fork to Anvo-Network** | Rebase onto upstream v2.6.2, rename SpringFormatter |
| `libraries/libfc/libraries/bls12-381` | **Fork to Anvo-Network** | Crypto lib, keep as submodule |
| `libraries/libfc/libraries/bn256` | **Fork to Anvo-Network** | Crypto lib, keep as submodule |

Already upstream (no action): boost, boringssl, prometheus-cpp, rapidjson, secp256k1.

## Renaming Categories

### Category 1: C++ Namespaces (HIGH EFFORT)

**`namespace eosio`** — 377 declarations across 366 files.

```cpp
// Current
namespace eosio { namespace chain { ... } }

// New
namespace core_net { namespace chain { ... } }
```

**Strategy:** Global find-and-replace with perl negative lookbehind.

- `namespace eosio` → `namespace core_net`
- `eosio::` → `core_net::` (but NOT `eosio::vm::` — see Category 5)
- `using namespace eosio` → `using namespace core_net`
- `EOSIO_` macro prefix → `CORE_NET_`

**Important:** Use `(?<![a-zA-Z_])eosio::` to avoid replacing `eosio` inside
identifiers like `apply_eosio_newaccount`.

### Category 2: Include Paths & Directory Structure (HIGH EFFORT)

**27 directories named `eosio`** → rename to `core_net`.

```cpp
// Current
#include <eosio/chain/controller.hpp>

// New
#include <core_net/chain/controller.hpp>
```

### Category 3: Executable Names (MEDIUM EFFORT)

| Current | New | Defined In |
|---------|-----|-----------|
| `nodeos` | `core_netd` | `CMakeLists.txt` |
| `cleos` | `core-cli` | `CMakeLists.txt` |
| `keosd` | `core-wallet` | `CMakeLists.txt` |
| `spring-util` | `core-util` | `CMakeLists.txt` |

**Downstream references:**
- ~1,939 `nodeos` references (test scripts, docs, CLI help text)
- ~560 `cleos` references (docs, test scripts, bash completion)
- ~186 `keosd` references (docs, test scripts)
- 25 test files with `nodeos_` in the filename → `core_netd_*.py`
- Program directories: `programs/nodeos/` → `programs/core_netd/`
- Doc directories: `docs/01_nodeos/` → `docs/01_core_netd/`

### Category 4: CMake & Build System (MEDIUM EFFORT)

**Files to update:**
- Root `CMakeLists.txt` — project name: `project( antelope-spring )` → `project( anvo-core )`
- `CMakeModules/eosio-config.cmake.in` → `core_net-config.cmake.in`
- `eosio.version.in` → `core_net.version.in`
- CMake target names: `eosio_chain` → `core_net_chain`, `eosio_testing` → `core_net_testing`

### Category 5: WASM Runtime / eos-vm (MEDIUM EFFORT)

After absorbing eos-vm into the repo:
- `eosio::vm::` namespace → `core_net::vm::`
- `eosio/vm/` include paths → `core_net/vm/`
- `EOS_VM_*` macros → `CORE_NET_VM_*`
- All unqualified `vm::` references fully qualified after moving out of `namespace eosio`

### Category 6: WASM Intrinsics & Injection (CRITICAL — RENAME BEFORE RELEASE)

These are protocol-level WASM function names. Renaming breaks compatibility with
existing compiled contracts, but since this is a new L1 with a compatibility mode,
they will be renamed:

- `eosio_assert` → `core_net_assert`
- `eosio_exit` → `core_net_exit`
- `eosio_injection.*` (61 functions) → `core_net_injection.*`

**Compatibility mode:** Genesis-configurable switch will allow migrating chains
to keep the old names. See [08_system_account_compatibility.md](08_system_account_compatibility.md).

### Category 7: System Account Names (GENESIS-CONFIGURABLE)

~698 occurrences of `eosio` as a system account name. These become
**genesis-configurable** rather than hard-renamed:

- New chains: `core`, `core.token`, `core.system`, `core.msig`, etc.
- Migrating chains: `eosio`, `eosio.token`, etc. (default for backward compatibility)

The `config::system_account_name` constants become functions that return the
genesis-configured value. See [08_system_account_compatibility.md](08_system_account_compatibility.md).

### Category 8: C++ Identifier Prefixes (MEDIUM EFFORT)

Identifiers using `eosio` as a prefix get renamed to `core_net_`:

- `eosio_system_tester` → `core_net_system_tester`
- `eosio_token_tester` → `core_net_token_tester`
- `eosio_contract_abi()` → `core_net_contract_abi()`
- `eosio_bios_wasm()` → `core_net_bios_wasm()`
- `eosio_root_key` → `core_net_root_key`

Wallet-related identifiers:
- `keosd_*` / `_keosd_*` → `core_wallet_*` / `_core_wallet_*`
- `make_keosd_signature_provider` → `make_core_wallet_signature_provider`
- `no_auto_keosd` → `no_auto_core_wallet`

### Category 9: Plugin Name Strings (MEDIUM EFFORT)

Plugin registration strings in Python tests and shell scripts:
- `"eosio::producer_plugin"` → `"core_net::producer_plugin"`
- `"eosio::net_plugin"` → `"core_net::net_plugin"`
- etc. (~37 Python test files, 2 shell scripts)

### Category 10: Documentation & Comments (~2,500+ occurrences)

- README.md, docs/ directory, code comments
- Config directory: `etc/eosio/` → `etc/core_net/`
- License headers

### Category 11: CI/CD & GitHub (SOFT)

- `.github/workflows/*.yaml`
- GitHub URLs pointing to `AntelopeIO/spring`
- Docker image names, package naming

## Execution Plan

### Phase 1: Submodule Absorption (DONE)
Absorb eos-vm, appbase, softfloat into the repo. Fork CLI11, bls12-381, bn256
to Anvo-Network GitHub org.

### Phase 2: Structural Renames (1-2 days)

1. Rename directories:
   - `include/eosio/` → `include/core_net/` (27 directories)
   - `programs/nodeos/` → `programs/core_netd/`
   - `programs/cleos/` → `programs/core-cli/`
   - `programs/keosd/` → `programs/core-wallet/`
   - `programs/spring-util/` → `programs/core-util/`

2. Rename files:
   - `eosio.version.in` → `core_net.version.in`
   - `CMakeModules/eosio-config.cmake.in` → `CMakeModules/core_net-config.cmake.in`
   - 25 `nodeos_*.py` test files → `core_netd_*.py`

### Phase 3: Content Replacement (2-3 days)

Apply via rebrand script (`_research/tools/rebrand.sh`):

1. Namespace: `eosio::` → `core_net::` (including absorbed eos-vm)
2. Include paths: `eosio/` → `core_net/`
3. Macros: `EOSIO_` → `CORE_NET_`
4. Executable names: nodeos → `core_netd`, cleos → `core-cli`, keosd → `core-wallet`
5. C++ identifiers: `eosio_*` → `core_net_*`, `keosd_*` → `core_wallet_*`
6. WASM intrinsics: `eosio_assert` → `core_net_assert`, etc.
7. Plugin strings, config paths, test harness references

### Phase 4: Genesis Account Switch (1-2 weeks)

Implement genesis-configurable system accounts per
[08_system_account_compatibility.md](08_system_account_compatibility.md).

### Phase 5: Verification & Cleanup

1. Full build + full test suite
2. Verify protocol-level behavior unchanged
3. Update README, documentation, CI/CD
4. Final verification

## Lessons Learned (from rebrand/core attempt)

1. **Absorb submodules first.** The first attempt hit cross-boundary issues with
   eos-vm's `eosio::vm::` namespace because it was still a submodule.
2. **Use perl negative lookbehind** for namespace replacement:
   `(?<![a-zA-Z_])eosio::` → `core_net::`. Never use plain `sed 's/eosio/core_net/g'`.
3. **Watch for double namespaces.** Config templates (`.hpp.in`) with
   `namespace eosio { namespace nodeos {` can both get renamed, creating
   `core_net::core_net::`.
4. **keosd stays as C++ prefix concept** — hyphens illegal in C++, so binary is
   `core-wallet` but identifier prefix is `core_wallet_`.
5. **Python generators** (like `gen_protocol_feature_digest_tests.py`) emit C++ with
   hardcoded namespace references — must be updated.
6. **INCBIN symbols** in `.cpp.in` template files need renaming too (they generate
   linker symbols from concatenated macro arguments).
7. **`boost::core` collision** — this is why `core::` was abandoned for `core_net::`.

## Timeline

| Phase | Duration | Dependencies |
|-------|----------|-------------|
| 1. Submodule absorption | Done | — |
| 2. Structural renames | 1-2 days | Phase 1 |
| 3. Content replacement | 2-3 days | Phase 2 |
| 4. Genesis account switch | 1-2 weeks | Phase 3 |
| 5. Verification & cleanup | 1-2 days | Phase 4 |
| **Total** | **~3-4 weeks** | |
