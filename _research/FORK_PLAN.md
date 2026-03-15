# Fork Plan: Anvo Network

## Vision

Build Anvo Network, a new Layer 1 blockchain that:
- Provides existing EOSIO chains (EOS, Telos, WAX, UX, etc.) a viable upgrade path
- Maintains full smart contract compatibility — no recompilation, no redeployment
- Closes the competitive gaps against modern L1s (Aptos, Sui, Solana)
- Runs on both x86_64 and AArch64 (ARM) from day one

## Starting Point

Fork of [Spring](https://github.com/AntelopeIO/spring) (AntelopeIO), the most current
C++ implementation of the Antelope protocol with Savanna (HotStuff-based BFT) consensus.

**What Spring gives us:**
- Production-proven blockchain node (years of mainnet operation across multiple chains)
- Savanna consensus with ~2-3 second finality via BLS12-381 quorum certificates
- WASM smart contract runtime with JIT/AOT compilation
- Decoupled proposer/finalizer architecture
- Plugin-based extensibility (22+ plugins)
- REST API infrastructure
- Comprehensive test suite (~870 C++ files, 70+ Python integration tests)

## Competitive Position

### Where Spring/Savanna Is Already Strong
- Finality: ~2-3s (faster than Ethereum, Solana, Cosmos)
- WASM contracts: mature tooling, C++ smart contracts
- Resource model: flexible CPU/NET/RAM staking (no gas fees)
- Proposer/finalizer separation: architecturally clean (Ethereum still working toward this)
- Accountable safety: BLS-based finality with explicit QCs

### Gaps to Close
1. **No parallel transaction execution** — single-threaded WASM, ~4-8K TPS ceiling
2. **x86_64 only** — no ARM server support (20-40% cheaper compute on AWS/Azure)
3. **No native cross-chain messaging** — unlike Cosmos (IBC) or Polkadot (XCMP)
4. **Ecosystem contraction** — developer tooling and community have shrunk since 2018
5. **Sub-second finality** — Sui (~400ms) and Aptos (~900ms) have pushed further

### Unique Advantage
**EOSIO compatibility.** No other project can offer existing EOSIO projects an upgrade path
to modern BFT consensus, parallel execution, and multi-architecture support. Thousands of
deployed contracts work unmodified.

---

## Technical Roadmap

### Phase 1: Foundation & Core Features (Months 1-4)

Everything needed to boot the chain with its differentiating features.
Gas model, passkey accounts, and baseline allocation are Phase 1 because
testnet T2 (Month 2-3) depends on them.

#### 1A. Fork, License & Rebrand (~2-3 weeks)
**Goal:** Fork under BSL, remove all legacy branding, establish new project identity.

**Status: COMPLETE** (branch `rebrand/core_net`, `make all` passes 100%)

**License: Business Source License 1.1** ✓ DONE
- Source-available from day one. Converts to Apache 2.0 after 3 years per version.
- Permitted: running nodes, building dApps, migrating EOSIO chains, private chains,
  research, education — everything except launching a competing public L1.
- Legal basis: Spring is MIT-licensed (Nov 2025), MIT permits sublicensing under BSL.
- **Detailed plan:** [18_licensing.md](18_licensing.md)

**Naming convention (finalized):**

| Layer | Name |
|-------|------|
| C++ namespace | `core_net::` |
| C++ identifier prefix | `core_net_` |
| Macro prefix | `CORE_NET_` |
| Include paths | `#include <core_net/chain/...>` |
| Node daemon | `core_netd` |
| CLI | `core-cli` |
| Wallet | `core-wallet` |
| Utility | `core-util` |
| CMake project | `anvo-core` |
| CMake targets | `core_net_chain`, `core_net_testing` |
| System accounts (new chains) | `core`, `core.token`, `core.bios`, etc. |
| System accounts (migrating) | `eosio`, `eosio.token`, etc. (genesis-configurable) |
| WASM intrinsics | `core_net_assert`, `core_net_exit` |
| WASM injection | `core_net_injection.*` |

**Why `core_net::` instead of `core::`:** `core::` collides with `boost::core::`.

**Rebrand:** ✓ DONE — ~11,000+ occurrences across ~870 files renamed.

**Submodule strategy:**

| Submodule | Origin | Action | Status |
|-----------|--------|--------|--------|
| `libraries/eos-vm` | AntelopeIO/eos-vm | **Absorbed** into repo, renamed `core_net::vm::` | ✓ DONE |
| `libraries/appbase` | AntelopeIO/appbase | **Absorbed** into repo | ✓ DONE |
| `libraries/softfloat` | AntelopeIO/berkeley-softfloat-3 | **Absorbed** into repo | ✓ DONE |
| `libraries/cli11/cli11` | AntelopeIO/CLI11 | **Forked to Anvo-Network**, rebase onto upstream v2.6.2 pending | ✓ Forked |
| `libraries/libfc/libraries/bls12-381` | AntelopeIO/bls12-381 | **Forked to Anvo-Network** | ✓ Forked |
| `libraries/libfc/libraries/bn256` | AntelopeIO/bn256 | **Forked to Anvo-Network** | ✓ Forked |
| `libraries/boost` | boostorg/boost | Keep as-is (upstream, 643MB) | ✓ No action |
| `libraries/libfc/libraries/boringssl/bssl` | boringssl.googlesource.com | Keep as-is (upstream) | ✓ No action |
| `libraries/prometheus/prometheus-cpp` | jupp0r/prometheus-cpp | Keep as-is (upstream) | ✓ No action |
| `libraries/rapidjson` | Tencent/rapidjson | Keep as-is (upstream) | ✓ No action |
| `libraries/libfc/secp256k1/secp256k1` | bitcoin-core/secp256k1 | Keep as-is (upstream) | ✓ No action |

**What was done (rebrand branch `rebrand/core_net`):**
1. ✓ Absorbed submodules: eos-vm, appbase, softfloat
2. ✓ Directory renames: 28 include/eosio/ → include/core_net/, program dirs, test files
3. ✓ Namespace: eosio:: → core_net:: across 870+ files
4. ✓ Include paths: eosio/ → core_net/
5. ✓ Macros: EOSIO_ → CORE_NET_, EOS_VM_ → CORE_NET_VM_
6. ✓ CMake: project, targets, executable names, runtime list names
7. ✓ C++ identifiers: eosio_* → core_net_*, keosd_ → core_wallet_*
8. ✓ WASM intrinsics: eosio_assert → core_net_assert, 61 injection functions
9. ✓ eos-vm internals: eosio::vm → core_net::vm, all macros/identifiers
10. ✓ Python tests, shell scripts, plugin strings, config paths
11. ✓ Full build passes: core_netd, core-cli, core-wallet, core-util, unit_test, plugin_test

**Rebrand script:** `_research/tools/rebrand.sh` — captures all fixes, repeatable from clean main.

**What was done (genesis accounts branch `feature/genesis-accounts`):**
12. ✓ Forked CLI11/bls12-381/bn256 to Anvo-Network org, updated .gitmodules
13. ✓ Genesis-configurable system accounts — ALL 7 steps complete:
    - system_accounts struct + global accessors (config.hpp/config.cpp)
    - 432+ call sites converted to function calls
    - SET_APP_HANDLER macro refactored to use config::system_account_name()
    - "eosio." prefix checks made configurable
    - Genesis JSON support (optional system_account_prefix field)
    - Persistence in global_property_object (snapshot v9, all 3 startup paths wired)
    - Test suite: 4 tests covering default/custom prefix, reserved names, factory methods
    - Bug fix: native handler registration moved after set_system_accounts()

**What was done (CLI11 upgrade):**
16. ✓ Switched CLI11 submodule from AntelopeIO fork (v2.2.0) to upstream CLIUtils/CLI11 (v2.6.2)
17. ✓ Created CoreNetFormatter in our own codebase (libraries/cli11/include/core_net/cli/formatter.hpp)
    — no CLI11 fork needed, custom formatter lives outside the submodule
18. ✓ Anvo-Network/CLI11 GitHub fork is orphaned (can be deleted)

**Submodule final state:**
- Absorbed into repo: eos-vm, appbase, softfloat
- Anvo-Network forks (shared crypto libs): bls12-381, bn256
- Upstream: boost, boringssl, prometheus-cpp, rapidjson, secp256k1, CLI11

**Phase 1A status: COMPLETE.** All code work done. Remaining items are non-code:
- Documentation (see below)
- CI/CD workflows
- New genesis configuration

**Documentation requirements:**

1. **Genesis configuration guide** — How to create a genesis.json for a new Anvo Network
   chain. Must cover:
   - The `system_account_prefix` field: what it does, when it's read (first boot only),
     how it becomes permanent chain state
   - Default behavior when omitted (eosio.* for migrating chains)
   - Example genesis.json with `"system_account_prefix": "core"` for new chains
   - Example genesis.json without the field for EOSIO chain migration
   - What system accounts are created (`<prefix>`, `<prefix>.null`, `<prefix>.prods`)
   - How the reserved prefix works (non-privileged accounts cannot create `<prefix>.*` names)

2. **Migration guide** — How existing EOSIO chains (EOS, Telos, WAX) import a snapshot
   and boot with their existing `eosio.*` accounts preserved. Covers:
   - Snapshot compatibility (v2-v8 snapshots auto-default to "eosio")
   - What changes vs. what stays the same

3. **Node operator guide** — Updated for new executable names (`core_netd`, `core-cli`,
   `core-wallet`, `core-util`), config directory (`etc/core_net/`), plugin names
   (`core_net::producer_plugin`, etc.)

4. **Developer guide** — Updated namespace (`core_net::`), include paths
   (`#include <core_net/...>`), macro prefix (`CORE_NET_`), API endpoint paths

5. **WASM ABI compatibility guide** — Critical documentation for contract developers:
   - **Dual-name intrinsics:** Legacy WASM intrinsic names (`eosio_assert`,
     `eosio_exit`, etc.) are permanently supported for backward compatibility.
     New names (`core_net_assert`, `core_net_exit`) are also registered.
     Both resolve to the same implementation.
   - **New intrinsics:** Any new WASM intrinsics added to the platform will be
     registered ONLY under `core_net_*` names. No `eosio_*` aliases for new functions.
   - **Existing contracts:** All contracts compiled against EOSIO CDT work unmodified.
     No recompilation needed.
   - **New contracts:** Should use `core_net_*` intrinsic names via the updated SDK/CDT.
   - **Injection functions:** Internal softfloat injection names (`_core_net_f32_add`,
     etc.) are purely runtime-internal — contracts never import them directly.

6. **README.md** — Project overview for Anvo Network, build instructions, quick start

#### 1A-OC. eos-vm-oc Component Rebrand
**Goal:** Remove remaining `eos` branding from the OC compiler component, completing
the Phase 1A rebrand for the now-functional AArch64+x86_64 OC runtime.

**Status: COMPLETE** (branch `rebrand/eos-vm-oc`, merged to main)

The Phase 1A rebrand covered the top-level namespace, macros, executables, and WASM
intrinsics but left the internal eos-vm-oc component untouched since it wasn't yet
ported to AArch64. Now complete — 61 files changed, all tests pass on both architectures.

**Naming convention:**

| Category | Old | New |
|----------|-----|-----|
| C++ namespace | `eosvmoc` | `corevmoc` |
| C functions | `eos_vm_oc_*` | `core_vm_oc_*` |
| C macros | `EOSVMOC_*` | `COREVMOC_*` |
| Directory paths | `eos-vm-oc/` | `core-vm-oc/` |
| File names | `eos-vm-oc.hpp`, `eos-vm-oc.h` | `core-vm-oc.hpp`, `core-vm-oc.h` |
| Log/error strings | `"EOS VM OC"` | `"Core VM OC"` |
| CMake variables | `CHAIN_EOSVMOC_SOURCES` | `CHAIN_COREVMOC_SOURCES` |
| Test suites | `eosvmoc_*_tests` | `corevmoc_*_tests` |
| Internal intrinsics | `eosvmoc_internal.*` | `corevmoc_internal.*` |
| CLI options | `--eos-vm-oc-*` | `--core-vm-oc-*` (primary) + `--eos-vm-oc-*` (deprecated alias) |

**What stays unchanged:**
- `CORE_NET_VM_OC_*` macros (already rebranded in Phase 1A)
- `core_net::` namespace qualifiers (already rebranded)
- `vm-oc` runtime name in config (neutral, no "eos")
- Process names (`oc-monitor`, `oc-trampoline`, `oc-compile`) — neutral

**Backward compatibility for migrating chains:**
- CLI options `--eos-vm-oc-cache-size-mb`, `--eos-vm-oc-enable`,
  `--eos-vm-oc-compile-threads`, `--eos-vm-oc-whitelist` preserved as deprecated
  aliases mapping to new `--core-vm-oc-*` names
- OC code cache is per-node and rebuilt on startup — no serialization compat concern
- `eosvmoc_internal.*` intrinsic names are runtime-internal (never in WASM imports);
  renaming requires bumping `current_codegen_version` to invalidate cached code

**Code cache migration:**
- New magic number: `COREVMOC` (0x434F4D5645524F43) for new chains
- Legacy magic: `EOSVMOC2` (0x32434F4D56534F45) accepted on read for migration
- Magic identifies file format; codegen version (bumped to 3) tracked per entry
- Cache is silently rebuilt on version mismatch — no operator action required

**Scope:** ~35 unique patterns, ~660+ occurrences across ~61 files. Directory renames
affect include paths throughout the codebase.

**Remaining VM rebrand work:** `eos_vm` → `core_vm` and `eos_vm_jit` → `core_vm_jit`
enum values and CLI flags (`--eos-vm`, `--eos-vm-jit`) need dual-name treatment on a
separate branch. See Phase 1A-VM below.

**Detailed architecture:** [20_core_vm_oc_architecture.md](20_core_vm_oc_architecture.md)

#### 1A-CDT. CDT Fork & Rebrand (required before chain launch)
**Goal:** Fork the Contract Development Toolkit to support `core_net::` namespaced
headers and build functions, enabling full end-to-end contract development.

**Status: PLANNED** — Required before contracts can be fully tested against the node.

**Why needed:**
- CDT provides the `eosio::` namespace (`eosio::contract`, `eosio::action`, etc.)
  used in all contract code via `#include <eosio/...>`
- CDT CMake functions (`find_package(spring)`, `eosio_check_version`,
  `add_eosio_test_executable`) reference Spring package names
- Contract tests link against the node's test framework via CDT's build system
- Without CDT fork, contracts compile but tests cannot link against our node

**Fork from:** `AntelopeIO/cdt` → private `Anvo-Network/cdt`

**Changes needed:**
- CMake package: `find_package(spring)` → `find_package(core)` (or dual-name)
- CMake functions: `add_eosio_test_executable` → `add_core_test_executable`
- SDK headers: provide `core_net::` namespace aliases alongside `eosio::`
- SDK headers: `#include <core_net/...>` wrappers around `#include <eosio/...>`
- Build scripts: Spring → Core references
- Keep backward compat: existing `eosio::` namespace must continue to work

**Not needed immediately:** Contracts compile with upstream CDT today. The fork is
needed for running contract tests against our node and for shipping a developer SDK.

#### 1B. System Contracts
**Goal:** Fork and modify the bundled system contracts for the new chain.

**Starting point:** Spring's `unittests/contracts/` — complete, tested contracts
with Savanna support (eosio.system, eosio.token, eosio.msig, eosio.wrap, eosio.boot).

**Modifications for launch (Phase 1):**
- Rename accounts (part of rebrand)
- Add gas pricing + collection (doc 11)
- Add baseline allocation on account creation (doc 11)
- Add refundable deposit system (doc 11)
- Replace Bancor RAM market with fixed pricing (doc 11)
- Hash-based account name derivation (doc 12)
- Boot sequence scripting for new chain genesis

**New contracts (Phase 3):**
- Identity registry + social vouching (doc 13)
- Attestation framework (doc 13)
- Social recovery (doc 12)
- Name service (doc 12)
- Data vault (doc 13)

**Detailed plan:** [14_system_contracts.md](14_system_contracts.md)

#### 1C. Resource Model: Gas + Staking + Baseline (~2-3 months)
**Goal:** Eliminate the onboarding cliff while preserving free-transaction capability.

**Model: Baseline + Staking + Parallel Gas**

Two parallel paths to the same resources, chosen per-transaction:

- **Staking path** (unchanged from today): stake tokens → get bandwidth allocation → free
- **Gas path** (new): pay per-transaction at governance-set prices → no staking needed

Plus:
- **Free baseline allocation** for every account (enough for casual daily use)
- **RAM market replaced** with governance-set pricing (no more Bancor speculation)
- **Refundable account deposit** with maturity conditions (anti-spam)

**How it works:**
- New optional `gas_payer` field on transactions (protocol feature)
- If set: resources billed at gas price, deducted from payer's token balance
- If not set: existing staking model applies unchanged
- dApps can sponsor gas for their users (gas_payer = contract account)
- Both paths coexist on the same chain

**Legacy EOSIO impact: zero breaking changes.**
- Protocol feature is opt-in (chains activate when ready)
- All existing transactions, contracts, and staking work unchanged
- Gas path is additive — new capability, nothing removed

**Unique competitive position:** No other L1 lets users choose between free
(staked) and paid (gas) transactions. Every other chain is gas-only.

**Detailed plan:** [11_resource_model_evolution.md](11_resource_model_evolution.md)

#### 1D. Passkey Accounts (~2-3 months, parallel with 1C)
**Goal:** Users create accounts with Face ID / fingerprint. No seed phrases, no wallet
apps, no browser extensions. The device IS the wallet. The biometric IS the key.

**What already exists in Spring:**
- WebAuthn (WA) key type with full P-256 signature verification (40+ tests)
- Permission system: hierarchical, weighted thresholds, mixed key types (K1+R1+WA)
- Key rotation via `updateauth`, hierarchical recovery (owner→active)

**What needs to be built:**
- Permissionless account creation with gas payment (depends on 1C)
- Hash-based account name derivation from passkey public key
- TypeScript SDK with WebAuthn integration
- Reference auth UI (passkey creation, device management, recovery)
- Social recovery contract (guardian-based, time-delayed)
- Name service contract (optional human-readable aliases)

**5-second onboarding:** Visit dApp → "Create Account" → Face ID → done.

**Competitive edge:** No other L1 has this at the protocol level. Ethereum
approximates it with ERC-4337 smart wallets (contract-level, fragmented standards,
expensive deployment). Every account is natively a smart wallet.

**Detailed plan:** [12_passkey_accounts.md](12_passkey_accounts.md)

#### 1E. LLVM Modernization (Prerequisite for OC + AArch64)
**Goal:** Update eos-vm-oc to build with modern LLVM (14+).

**Status: COMPLETE** (branch `llvm/modernize-14`, build passes 100% with `-DENABLE_OC=ON`)

**Problem solved:** Spring required LLVM 7-11 for eos-vm-oc. Ubuntu 24.04 ships
LLVM 14-20. The ORCv1 JIT API was completely removed in LLVM 12, along with several
other breaking changes in the LLVM C++ API.

**What was changed:**
- `CMakeLists.txt`: version check now accepts LLVM 7-11 **and** 14-17
- `LLVMJIT.cpp`: Full ORCv2 JIT implementation for LLVM 12+ with `#if` guard:
  - `RTDyldObjectLinkingLayer` (ORCv2) with `GetMemoryManager` factory
  - `IRCompileLayer` (ORCv2) with `std::unique_ptr<IRCompiler>`
  - `ExecutionSession` with `SelfExecutorProcessControl` (required by LLVM 14)
  - `JITDylib` + `ThreadSafeModule` for module submission
  - `MemoryManagerForwarder` to bridge ORCv2's `unique_ptr` factory to shared `UnitMemoryManager`
  - `createConstantPropagationPass()` → `createSCCPPass()` (removed in LLVM 12)
  - `F_Text` → `OF_Text` (renamed in LLVM 12)
  - Conditional includes for removed headers (`LambdaResolver.h`, `NullResolver.h`)
- `LLVMEmitIR.cpp`: Compatibility helpers for LLVM 14's typed pointer requirements:
  - `EmitLoad()` / `EmitInBoundsGEP()` wrappers (17 CreateLoad + 7 CreateInBoundsGEP calls)
  - Fixed include order (`llvm/Pass.h` before `InstCombine.h`)
- `gs_seg_helpers.c`: Fixed Phase 1A rebrand misses (only compiled with OC enabled):
  - `#include` path: `eosio/` → `core_net/`
  - Macros: `EOS_VM_OC_*` → `CORE_NET_VM_OC_*`

**All changes are backward-compatible with LLVM 7-11** via `#if LLVM_VERSION_MAJOR` guards.

**Build verified:** Ubuntu 24.04, GCC 13.3, LLVM 14.0.6, `-DENABLE_OC=ON -DCMAKE_BUILD_TYPE=Release`

**Remaining LLVM work (future):**
- LLVM 15+: opaque pointers become default — `getPointerTo(N)` and `getPointerElementType()`
  will need to be replaced. This is a larger refactor of `LLVMEmitIR.cpp`.
- LLVM 16+: `llvm/Support/Host.h` moves to `llvm/TargetParser/Host.h`

#### 1F. AArch64 Support (depends on 1E)
**Goal:** Production-quality ARM server support for eos-vm-oc runtime.

**Status: COMPLETE** (branch `aarch64/eos-vm-oc`, ~30 commits)

**Strategy:** Dedicated register X28 via `-ffixed-x28`, matching V8/SpiderMonkey/Wasmtime.

**What's done (builds 100% on both x86_64 and AArch64):**

| Component | File(s) | Status |
|-----------|---------|--------|
| Build system | `CMakeLists.txt` (root + chain) | ✓ OC enabled on aarch64, `-ffixed-x28` global |
| GS abstraction | `gs_seg_helpers.h/c` | ✓ Arch-conditional GS_PTR, X28 inline asm get/set |
| Stack switching | `switch_stack_linux_aarch64.s` | ✓ New AArch64 assembly with AAPCS64 frame save |
| Signal handler | `executor.cpp` | ✓ Reads X28 from ucontext on AArch64 |
| LLVM IR generation | `LLVMEmitIR.cpp` | ✓ VMEM_ADDR_SPACE constant, emitVmemPointer(), resolveVmemPtr(), per-function X28 load via llvm.read_register |
| LLVM JIT target | `LLVMJIT.cpp` | ✓ `+reserve-x28` target feature for codegen |
| Host function asm | `eos-vm-oc.hpp` | ✓ 5 inline asm blocks ported (array_ptr, null_term, convert_native, depth check, ctx load) |
| Memory layout | `memory.cpp` | ✓ Runtime 4KB page size assertion for AArch64 |
| CRC32 | `city.cpp` | ✓ ARM CRC32 intrinsic via `arm_acle.h` |
| Pagemap | `pagemap_accessor.hpp` | ✓ Enabled on aarch64 |
| Tests | `eosvmoc_platform_tests.cpp` | ✓ 4 new tests (register r/w, memory constants, smoke, multi-contract) |
| Test config | `unittests/CMakeLists.txt` | ✓ Guard nofsgs tests to x86_64 only |
| zlib fix | `libraries/libfc/test/CMakeLists.txt` | ✓ Explicit ZLIB dep for test_fc (ARM link order) |
| ORCv2 context ownership | `LLVMEmitIR.cpp`, `LLVMJIT.cpp` | ✓ Heap-allocate LLVMContext for proper ThreadSafeContext ownership |
| ORCv2 finalizeMemory | `LLVMJIT.cpp` | ✓ Fix return value (false=success, was returning true=error) |
| Stack sizes check | `LLVMJIT.cpp` | ✓ Allow extra .stack_sizes entries from AArch64 outlined helpers |
| tee_local fix | `LLVMEmitIR.cpp` | ✓ Don't resolve local pointers as vmem globals |
| Table access via CB | `eos-vm-oc.h`, `ipc_protocol.hpp`, `eos-vm-oc.hpp`, `compile_trampoline.cpp`, `compile_monitor.cpp`, `executor.cpp`, `LLVMEmitIR.cpp` | ✓ Load table base from control block via X28 (avoids ADRP page-alignment issue) |
| Stack sizes trailing | `LLVMJIT.cpp` | ✓ Handle trailing address at end of .stack_sizes section |

**Bugs found and fixed during debug session (2026-03-15):**

1. **Heap corruption (SIGABRT):** The file-scope `LLVMContext` in LLVMEmitIR.cpp was
   wrapped in a `unique_ptr` and given to ORCv2's `ThreadSafeContext`. When ORCv2
   moved the `ThreadSafeModule` during `IRCompileLayer::emit`, the source destructor
   called `delete` on the non-heap context → heap corruption → SIGABRT in `__libc_free`.
   Fix: `llvm::LLVMContext& context = *new llvm::LLVMContext;` (heap allocation).
   This bug was latent on x86_64 (ORCv1 path didn't trigger it).

2. **Materialization failure:** `UnitMemoryManager::finalizeMemory()` returned `true`
   (= error in LLVM's RTDyldMemoryManager convention). The ORCv1 path never checked
   this return value, but ORCv2's `RTDyldObjectLinkingLayer` does — it reported
   "Failed to materialize symbols" for all wasmFunc* symbols even though code was
   successfully compiled and loaded. Fix: return `false` (= success). This bug was
   also latent on x86_64.

3. **Stack sizes assertion:** LLVM's AArch64 backend generates outlined save/restore
   helper functions that produce extra `.stack_sizes` entries (225 entries for 70 WASM
   functions). The strict `==` check caused `_exit(1)`. Fix: changed to `>=` (require
   at least as many entries as function defs).

4. **tee_local vmem resolution (SIGSEGV):** `tee_local` in LLVMEmitIR.cpp called
   `get_mutable_global_ptr()` on local variable pointers (stack allocas). On x86_64
   `resolveVmemPtr` is a no-op so this was harmless, but on AArch64 it added X28 to
   the stack address → garbage pointer → SIGSEGV. Found by dumping optimized LLVM IR
   which showed `ptrtoint(alloca) + X28` pattern. Fix: store directly to
   `localPointers[]`, matching `set_local` behavior.

5. **ADRP page-relative table access (indirect call type mismatch):** On AArch64,
   ADRP+ADD is page-relative — the encoded page difference is only correct if the
   code blob is loaded at the same 4KB page alignment as where LLVM compiled it. The
   OC code cache allocator doesn't guarantee page alignment (uses 16-byte aligned
   `rbtree_best_fit`), so ADRP+ADD references to the `wasmTable` global pointed to
   wrong memory → type descriptor mismatch → "Indirect call function type mismatch".
   Fix: store `table_base` in the control block (accessible via X28) and load it
   per-function instead of using a PC-relative global reference. Added `table_offset`
   to `code_compilation_result_message`, `code_descriptor`, and the IPC path.

6. **Trailing .stack_sizes entry:** The `.stack_sizes` section on AArch64 can end with
   a trailing address that fills remaining space exactly, with no room for the
   following ULEB128 stack size value. Fix: break parse loop when address read reaches
   section boundary.

**OC compilation and execution now works on AArch64.** IR verification passes, LLVM
codegen produces position-independent code, ORCv2 materialization completes, indirect
calls resolve correctly, and compiled WASM contracts execute successfully.

**AArch64 test results (44 → 8 failures):**
- ✓ `eosvmoc_platform_tests` — 4/4 PASS
- ✓ `eosvmoc_limits_tests` — 6/6 PASS
- ✗ `wasm_part1_tests --eos-vm-oc` — **8 failures** (down from 44):
  - `f64_tests` (×2), `f64_test_bitwise` (×2), `f64_test_cmp` (×2),
    `f32_f64_conversion_tests` (×2)

7. **Page-aligned code allocation (constant pool references):** Same root cause as
   bug 5 — ADRP page-relative addressing also affects constant pool and literal
   pool references within the code blob. Float constants loaded via ADRP+LDR from
   a misaligned blob produced garbage values, causing f64 test failures. Fix:
   page-align the compilation buffer (posix_memalign) AND the code cache allocator
   (4096-byte alignment instead of 16-byte default).

**All tests now pass on AArch64:**
- `eosvmoc_platform_tests` — 4/4 PASS
- `eosvmoc_limits_tests` — 6/6 PASS
- `wasm_part1_tests --eos-vm-oc` — ALL PASS (0 failures)

**x86_64 tests also pass** — no regressions from the AArch64 changes.

**Debug instrumentation removed.** All temporary logging, crash handlers, and
IR dump helpers have been cleaned up. Only the actual fixes remain.

**ARM test server:** `ubuntu@34.213.225.55` (Ubuntu 24.04, aarch64, 8 cores, 15GB RAM)

**Detailed plan:** [06_aarch64_port_implementation.md](06_aarch64_port_implementation.md)

**Expected performance:** 85-105% of x86_64 on AWS Graviton4.

#### 1F. Testnet Strategy
**Goal:** Phased testnet proving each layer before adding the next.

| Phase | When | What | Who |
|---|---|---|---|
| T1: Internal | Month 1-2 | Rebranded node + existing contracts (no OC) | Us |
| T2: Features | Month 2-3 | Gas + passkeys + baseline + LLVM modernization | Us + trusted devs |
| T3: Infra | Month 3-4 | ARM + OC runtime + bridge + monitoring | Us + bridge operators |
| T4: Public | Month 4-6 | SDK + docs + explorer + faucet | Open |
| T5: Stress | Month 8-10 | Block-STM + load testing | Open |

**Mainnet readiness:** Security audit, 30+ days uninterrupted testnet, migration
tested with real chain snapshot, 5+ independent validators.

**Detailed plan:** [15_testnet_strategy.md](15_testnet_strategy.md)

---

### Phase 2: Parallel Execution (Months 3-9)

**Goal:** Block-STM parallel transaction execution for 4-8x throughput improvement.

**Confirmed approach: Block-STM** (optimistic parallel execution with conflict detection).

#### Why Block-STM

Four parallel execution strategies were evaluated:

| Strategy | Throughput | Protocol Change | Contract Change | Proven |
|----------|-----------|----------------|-----------------|--------|
| **Block-STM** | **4-8x** | **None** | **None** | **Yes (Aptos)** |
| Scope Partitioning | 2-4x | Possibly | Possibly | No (never shipped) |
| Read-Only Parallelism | 2-3x (reads only) | None | None | Yes (trivial) |
| State Sharding | Near-linear | Major | Likely | Partially |

Block-STM was selected because:
1. **Zero breaking changes** — blocks, receipts, Merkle roots are identical to sequential execution.
   This is non-negotiable for the EOSIO upgrade path. Existing contracts run unmodified.
2. **Proven in production** — Aptos shipped it, published the research, demonstrated 4-16x
   gains on real workloads with 32 threads.
3. **Best fit for EOSIO workloads** — most blocks contain transactions touching different
   accounts and contracts. Low natural conflict rate = high parallelism.
4. **WASM layer is already ready** — per-thread executors, memory, allocators exist. The
   work is in the state layer (shadow state, scheduler), not the VM.
5. **Doesn't touch ChainBase** — shadow state sits above it, avoiding the riskiest refactor.
6. **Graceful degradation** — worst case (all conflicts) = sequential with small overhead.
   Ship with feature flag, zero downside risk.

Scope partitioning was the original EOSIO whitepaper vision but was never implemented.
It breaks down for real DeFi transactions that span multiple scopes (e.g., a DEX trade
touches the DEX contract scope AND two token contract scopes). Block-STM handles these
naturally through conflict detection and re-execution.

State sharding was ruled out — the engineering cost is 12-24 months, it requires protocol
changes, and cross-shard composability remains an unsolved problem industry-wide.

#### Architecture

```
Block (N transactions)
        |
   Scheduler (deterministic ordering)
        |
   +---------+---------+---------+
   |         |         |         |
Worker 0  Worker 1  Worker 2  Worker 3
   |         |         |         |
shadow_0  shadow_1  shadow_2  shadow_3
   |         |         |         |
   +----+----+----+----+----+----+
        |              |
  Multi-Version    Conflict
  Data Structure   Detector
        |              |
   Commit (in tx order) / Abort+Retry
```

#### Implementation Phases

**Phase 2A: Read-Only Parallelism — Warmup (Month 3, ~2-4 weeks)**
- Separate read-only queries from state-modifying transactions
- Execute read-only transactions in parallel against a state snapshot
- Gets the team familiar with Spring's threading model
- Delivers immediate, low-risk value
- Foundation infrastructure reused by Block-STM (thread pools, per-thread contexts)

**Phase 2B: Deferred Sequence Assignment (Month 3-4, ~2-3 weeks)**
- Remove the `global_action_sequence` bottleneck (100% conflict rate today)
- Execute transactions without assigning sequences
- Assign `global_sequence`, `recv_sequence`, `auth_sequence` in a deterministic
  post-execution pass based on transaction index
- **This alone unblocks all other parallelization work**
- Can be developed and tested in isolation against sequential execution
  (verify identical receipts)

**Phase 2C: Multi-Version Store + Shadow State Layer (Month 4-6, ~6-8 weeks)**
- Multi-version data structure indexed by `(state_key, tx_index, incarnation)`
- Per-transaction shadow state overlay between `apply_context` and ChainBase
- Each worker thread gets its own `apply_context` + `transaction_shadow_state`
- Database operations (`db_store_i64`, `db_get_i64`, etc.) route through shadow state
- Read path: own writes → multi-version store → ChainBase base state
- Write path: buffer in shadow state, commit to ChainBase in order
- State keys: `(table_id, primary_key)` for contract state,
  `account_name` for system state
- Both components can be developed in parallel and tested independently

**Phase 2D: Scheduler & Conflict Detection (Month 6-7, ~4-6 weeks)**
- Worker threads execute transactions optimistically
- Read/write sets tracked per transaction
- Validation pass checks that all reads are still valid
- Aborted transactions re-execute with updated state
- Deterministic ordering ensures identical results across nodes

**Phase 2E: Controller Integration & Tuning (Month 7-9, ~4-6 weeks)**
- Wire Block-STM into `controller_impl` block production path
- Feature flag for sequential fallback
- Parallel validation for non-producing nodes
- Performance tuning (thread count, batch sizes, contention optimization)
- Scope-based pre-sorting: use historical data or static analysis to pre-sort
  transactions into likely-non-conflicting batches, reducing re-execution rate

#### Key Files Modified

| File | Change |
|------|--------|
| `controller.cpp` | Parallel execution path in block production |
| `transaction_context.cpp` | Shadow state undo sessions |
| `apply_context.cpp` | Route db ops through shadow state |
| `apply_context.hpp` | Add shadow state member |

#### New Files

| File | Purpose |
|------|---------|
| `block_stm/scheduler.hpp/cpp` | Transaction scheduler |
| `block_stm/multi_version_store.hpp/cpp` | Multi-version data structure |
| `block_stm/shadow_state.hpp/cpp` | Per-transaction state overlay |
| `block_stm/conflict_detector.hpp/cpp` | Read/write set validation |

**No contract changes required.** Shadow state is transparent to WASM execution.
Contracts interact with chain state exclusively through host functions (`db_store_i64`,
`db_find_i64`, etc.). What happens beneath those functions — whether it's a direct
ChainBase write or a shadow state buffer — is invisible to the contract.

**Detailed analysis:**
- [01_chainbase_internals.md](01_chainbase_internals.md) — ChainBase barriers
- [02_transaction_execution_pipeline.md](02_transaction_execution_pipeline.md) — State mutation map
- [03_wasm_threading_model.md](03_wasm_threading_model.md) — VM threading readiness
- [04_block_stm_integration_plan.md](04_block_stm_integration_plan.md) — Full implementation plan

#### Expected Results

| Workload | Speedup | Notes |
|----------|---------|-------|
| Token transfers (different accounts) | 4-8x | Low conflict, high parallelism |
| NFT mints (different collections) | 4-8x | Independent state |
| DEX trades (same market) | 2-4x | Moderate conflict |
| Sequential dependencies | ~1x | Graceful degradation |

---

### Phase 3: Ecosystem & Differentiation (Months 6-18)

These items can be pursued in parallel with Phase 2. Gas model, passkey accounts,
and resource model changes are already in Phase 1.

#### 3A. Migration Tooling (~2-3 months)
**Goal:** Provide existing EOSIO chains a verified, documented upgrade path.

**Snapshot migration:** Chains on Spring/Leap 3.x+ can snapshot and import directly.
Older chains (EOSIO 2.x) must upgrade to Leap first (well-documented existing process).

**Migration contract:** Handles state transformation for each migrating chain:
- Convert old resource model → new model (gas + baseline + deposits)
- Archive Bancor RAM market → fixed-price allocation
- Preserve all accounts, balances, permissions, contract data
- Batched processing to avoid timeout on large chains

**Tools:**
- Snapshot validator (pre-import compatibility check)
- Migration contract (state transformation)
- Migration verifier (pre/post comparison)
- Per-chain migration guide

**Key guarantee:** Any contract that worked on the source chain works on the fork.
Token transfers, staking, voting, multisig — all must match or exceed.

**Detailed plan:** [17_migration_mechanics.md](17_migration_mechanics.md)

#### 3B. Cross-Chain Bridges: Ethereum & Bitcoin
**Goal:** Trustless bridges to ETH and BTC — where the liquidity and users are.

**Key finding:** The cryptographic infrastructure is already in place on both sides.
Savanna uses BLS12-381 — the same curve as Ethereum's beacon chain. Smart contracts
already have access to Keccak-256, secp256k1, BLS pairing, SHA-256, and RIPEMD-160.

**Anvo → Ethereum (trustless):**
- Savanna QCs (~200 bytes) are verifiable on Ethereum via EIP-2537 BLS precompile
- ~360K gas per finality proof verification
- State history plugin already exports all data needed for relay infrastructure

**Ethereum → Anvo (trustless):**
- Beacon chain sync committee BLS signatures verifiable via `bls_pairing` intrinsic
- Merkle-Patricia trie verifier needed as contract code (~500-1000 lines)

**Bitcoin → Anvo (SPV-secure):**
- Double-SHA256, RIPEMD-160, secp256k1 all available as intrinsics
- Standard SPV light client pattern in contract code

**Anvo → Bitcoin (federated):**
- Bitcoin can't verify external proofs natively — requires multisig federation
- Same limitation every BTC bridge faces

**Existing asset: Libre Crosslink** (`/opt/dev/libre-crosslink/`)
- Production-grade multi-chain bridge already handling BTC and ETH/USDT
- Go bridge node with TSS/MPC threshold signing (no single node holds keys)
- 5-node testnet mesh validated, 14 ADRs documenting security architecture
- Bridge node is **chain-agnostic** — porting to Spring fork is config changes only
- Smart contracts (C, EOSIO CDT) need redeployment with updated account names

**Effort WITH Crosslink:** ~3-4 months (port, Savanna finality integration, remaining work)
**Effort WITHOUT Crosslink:** ~11 months (from scratch)
**Savings:** ~7-8 months of engineering time.

**Detailed plans:**
- [09_cross_chain_eth_btc.md](09_cross_chain_eth_btc.md) — Crypto primitive analysis
- [10_libre_crosslink_integration.md](10_libre_crosslink_integration.md) — Crosslink integration plan

#### 3C. Identity & Data Ecosystem (Months 9-18)
**Goal:** Give every person sovereignty over their digital identity and data.

**The Stack:**
```
Applications (social, governance, DeFi, marketplaces)
     ↑ consume identity + data with user permission
Data Ecosystem (vaults, selective disclosure, data marketplace)
     ↑ user-controlled encrypted storage
Identity (personhood, attestations, reputation, social graph)
     ↑ layered proof of personhood
Chain (passkey accounts, permissions, gas model, bridges)
```

**Identity — Layered Proof of Personhood:**
- Layer 0: Account exists (deposit paid) — baseline
- Layer 1: Hardware attestation (passkey device proof) — medium confidence
- Layer 2: Social vouching (web of trust, N verified users vouch) — medium-high
- Layer 3: Biometric uniqueness (ZK proof, no biometric on-chain) — very high
- Layer 4: Credential attestation (KYC, gov ID, degrees — hash anchored) — configurable
- Layer 5: Reputation accrual (account age, activity, community standing) — emergent

Applications declare which layers they require. Users accumulate signals over time.
Permission system gates actions by identity level via existing `linkauth` mechanism.

**Data Ecosystem:**
- User-controlled encrypted data vaults (profile, social, financial, activity)
- Selective disclosure (grant/revoke per-field, per-application)
- Verifiable credentials (W3C VC compatible attestations)
- Data marketplace (users sell access to their data, earn tokens directly)
- Full portability (switch apps without losing identity, data, or reputation)

**What This Enables:**
- One-person-one-vote governance (not stake-weighted)
- Sybil-resistant airdrops and quadratic funding
- UBI distribution to verified unique humans
- Reputation-based DeFi (under-collateralized lending)
- Social platforms without platform risk (you own your data)

**Detailed plan:** [13_identity_data_ecosystem.md](13_identity_data_ecosystem.md)

#### 3D. Integrated Indexer / API Node (~4-5 months)
**Goal:** Build the indexer into the node distribution as a first-class node type,
not a bolted-on external service.

**Concept:** An `index-api` node mode that combines block production/validation with
built-in indexing and a rich query API. Operators run one binary, get full indexing
out of the box.

**Why:**
- EOSIO's biggest operational pain point: running nodeos + Hyperion + Elasticsearch
  as separate services, each with their own config, scaling, and failure modes
- Other chains ship indexing as part of the node (Solana's RPC, Sui's indexer)
- Identity and data ecosystem features need purpose-built queries that generic
  indexers can't provide
- Simplifies developer onboarding: one endpoint for everything

**Architecture:**
- New node plugin: `index_api_plugin` (alongside existing chain_api_plugin)
- Consumes state history internally (no WebSocket hop)
- Stores indexed data in embedded PostgreSQL or SQLite
- Serves rich API: action history, token balances, identity queries, gas analytics
- Optional: can be disabled for pure validator nodes (no indexing overhead)

**Indexed data (MVP):**
- Action history per account
- Token transfer history
- Account lifecycle events (creation, permission changes)
- Gas payment records
- Identity attestations and vouching events
- Contract table change history

**Detailed plan:** [16_indexing_api_layer.md](16_indexing_api_layer.md)

#### 3E. Developer Experience
- Modern SDK (TypeScript/Rust) with passkey + gas + identity support
- Block explorer built on index-api endpoints
- Contract development toolkit with testing framework
- Integration with popular wallets

#### 3F. Sub-Second Finality (Research)
- Savanna's two-chain rule requires 2 QC rounds (~2-3s)
- Aptos achieves ~900ms with pipelined HotStuff from the same lineage
- Investigate: single-chain finality, pipelined QC aggregation
- This would be a protocol-level change — approach with care

---

## Compatibility Guarantees

### What MUST NOT Break (Sacred)
- WASM smart contract execution (existing contracts run unmodified)
- Core chain API endpoints (existing SDKs and tools keep working)
- Account and permission model
- Action and transaction format
- Table access patterns (`db_*_i64` host functions)
- ABI serialization format

### What CAN Evolve
- Consensus parameters (block time, finalizer count, QC thresholds)
- Resource model (billing, limits, staking mechanics)
- System contracts (governance, voting, resource allocation)
- Node operator interfaces (config, monitoring, deployment)
- Header extensions (new metadata without breaking existing parsers)

### Migration Path for Existing Chains
1. Chain takes state snapshot on current EOSIO/Antelope version
2. Snapshot imported into new chain's genesis
3. All accounts, balances, deployed contracts, table data preserved
4. Chain boots with new consensus and runtime
5. Existing contracts execute without recompilation

---

## Team Requirements

**Note:** Libre Crosslink was built in ~5 weeks by a solo developer working with
Claude. These team estimates are for the full scope if expanding beyond that model.

### Core (Months 1-6)
- **1-2 senior C++ developers** — systems programming, blockchain internals
- **1 SDK/tooling developer** — TypeScript SDK, WebAuthn integration
- **1 build/DevOps engineer** — CI/CD, multi-arch builds, packaging

### Expanded (Months 6-12)
- **1-2 additional C++ developers** — Block-STM, indexer plugin
- **1 documentation/community lead** — migration guides, ecosystem outreach

### Key Expertise Needed
- LLVM / compiler internals (for LLVM modernization + AArch64 port)
- Concurrent data structures (for Block-STM)
- EOSIO/Antelope protocol knowledge (for compatibility and migration)
- AArch64 architecture (for ARM port)

### Build Environment
- **x86_64:** Ubuntu 24.04, GCC 13.3, CMake 3.28, LLVM 14.0.6 — verified 100%, all tests pass
- **AArch64:** Ubuntu 24.04, GCC 13.3, CMake 3.28, LLVM 14.0.6 — verified 100%, all tests pass
- **Core VM OC:** Builds with LLVM 7-11 or LLVM 14-17. Ubuntu 24.04 uses `llvm-14-dev`.
- **Full build:** `-DENABLE_OC=ON -DCMAKE_BUILD_TYPE=Release` — all runtimes
- **AArch64 runtimes:** interpreter (vm) + OC (vm-oc). No vm-jit on ARM (no backend).

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Block-STM overhead in low-parallelism workloads | Medium | Low | Feature flag for sequential fallback |
| AArch64 performance regression | Low | Medium | Graviton benchmarking throughout development |
| EOSIO compatibility breaks | Low | High | Comprehensive contract test suite from existing chains |
| Existing EOSIO chains don't adopt | Medium | High | Strong migration tooling, clear value proposition |
| LLVM version compatibility issues | ~~High~~ **Resolved** | ~~High~~ **Done** | ✓ Patched in Phase 1E. Now supports LLVM 7-11 and 14-17. LLVM 15+ (opaque pointers) is future work. |
| ChainBase limitations block Block-STM | Medium | Medium | Shadow state avoids modifying ChainBase directly |

---

## Success Metrics

### Technical
- [ ] All existing EOSIO contract test suites pass unmodified
- [ ] 4x+ throughput improvement on realistic workloads (Block-STM)
- [ ] <15% performance gap between x86_64 and AArch64
- [ ] <3 second finality maintained
- [ ] Snapshot migration from EOS mainnet demonstrated

### Ecosystem
- [ ] At least one existing EOSIO chain commits to testing migration
- [ ] SDK available in TypeScript and Rust
- [ ] Block explorer operational
- [ ] Documentation covers migration, development, and node operation

---

## Research Documents Index

| Doc | Topic |
|-----|-------|
| [01](01_chainbase_internals.md) | ChainBase memory model, undo stack, concurrency barriers |
| [02](02_transaction_execution_pipeline.md) | Transaction execution flow, state mutations, dependency graph |
| [03](03_wasm_threading_model.md) | WASM runtime threading, per-thread isolation, host functions |
| [04](04_block_stm_integration_plan.md) | Block-STM architecture, shadow state, scheduler design |
| [05](05_architecture_portability.md) | x86_64 dependency inventory, porting strategies |
| [06](06_aarch64_port_implementation.md) | AArch64 port file-by-file implementation plan |
| [07](07_rebrand_plan.md) | Rebrand inventory (~11K refs), execution plan, protocol-safety boundaries |
| [08](08_system_account_compatibility.md) | Genesis-configurable system account names, compatibility switch design |
| [09](09_cross_chain_eth_btc.md) | Ethereum & Bitcoin bridge design, crypto primitive inventory, relay architecture |
| [10](10_libre_crosslink_integration.md) | Libre Crosslink integration — existing BTC/ETH bridge, porting plan, 7-8mo savings |
| [11](11_resource_model_evolution.md) | Baseline + Staking + Parallel Gas resource model, RAM market removal, legacy impact |
| [12](12_passkey_accounts.md) | Passkey-first accounts, WebAuthn infrastructure, 5-second onboarding, social recovery |
| [13](13_identity_data_ecosystem.md) | Identity + data platform: layered personhood, data vaults, selective disclosure, marketplace |
| [14](14_system_contracts.md) | System contract starting point, required modifications, boot sequence |
| [15](15_testnet_strategy.md) | Phased testnet strategy: internal → features → infra → public → stress |
| [16](16_indexing_api_layer.md) | Integrated indexer as node plugin, API layer, block explorer |
| [17](17_migration_mechanics.md) | EOSIO chain migration: snapshot format, state transformation, migration contracts |
| [18](18_licensing.md) | BSL 1.1 license: terms, Additional Use Grant, change date, contributor DCO, messaging |
| [19](19_genesis_accounts_implementation.md) | Genesis-configurable accounts implementation details |
| [20](20_core_vm_oc_architecture.md) | Core VM OC: architecture, code cache format, AArch64 port, migration guide |
