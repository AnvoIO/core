# Session Bootstrap — Anvo Network Core

Paste this at the start of a new Claude Code session to restore context.

---

## Project Overview

We're building **Anvo Network** — a new L1 blockchain forked from Spring (AntelopeIO/EOSIO). The repo is at `/opt/dev/spring` with a private GitHub remote at `https://github.com/Anvo-Network/core`.

## Current State

**Branch:** `main` — Phase 1A and 1E complete and merged.
**Next task:** Phase 1F — AArch64 Port (depends on 1E, now unblocked)

### Phase 1A: COMPLETE (on main)
- Full rebrand: core_net:: namespace, CORE_NET_ macros, core_netd/core-cli/core-wallet/core-util
- 3 submodules absorbed (eos-vm, appbase, softfloat), CLI11 switched to upstream v2.6.2
- Genesis-configurable system accounts (12 tests passing)
- Backward compat: dual WASM intrinsics, dual ABI version strings
- Documentation: README, genesis guide, migration guide, node ops, dev guide, WASM compat
- Build passes 100% with -DENABLE_OC=OFF

### Phase 1E: COMPLETE (on main)
- LLVM modernization: eos-vm-oc now builds with LLVM 14 (ORCv1→ORCv2 migration)
- CMake accepts LLVM 7-11 or 14-17
- LLVMJIT.cpp: Full ORCv2 JIT with ExecutionSession/JITDylib/ThreadSafeModule/SelfExecutorProcessControl
- LLVMEmitIR.cpp: EmitLoad()/EmitInBoundsGEP() helpers for typed pointer API (17+7 call sites)
- gs_seg_helpers.c: Fixed Phase 1A rebrand misses (only compiled with OC enabled)
- All changes backward-compatible with LLVM 7-11 via #if guards
- Build passes 100% with -DENABLE_OC=ON on Ubuntu 24.04 + LLVM 14.0.6

### What remains:
- Phase 1F: AArch64 port (depends on 1E ✓)
- Phase 1B: System contracts
- Phase 1C: Resource model (gas + staking)
- Phase 1D: Passkey accounts
- CI/CD workflows
- New genesis configuration

### Future LLVM work (not blocking):
- LLVM 15+: opaque pointers — `getPointerTo(N)` and `getPointerElementType()` need replacing
- LLVM 16+: `llvm/Support/Host.h` → `llvm/TargetParser/Host.h`

## Naming Convention

| Layer | Name |
|-------|------|
| C++ namespace | `core_net::` |
| Macros | `CORE_NET_`, `CORE_NET_VM_` |
| Node daemon | `core_netd` |
| CLI / Wallet / Util | `core-cli`, `core-wallet`, `core-util` |
| System accounts | genesis-configurable (`core.*` or `eosio.*`) |

## Key Research Docs
- `_research/FORK_PLAN.md` — master plan
- `_research/05_architecture_portability.md` — x86_64 dependency inventory
- `_research/06_aarch64_port_implementation.md` — AArch64 port plan (depends on 1E ✓)

## Build
```bash
cd /opt/dev/spring && mkdir build && cd build
cmake -DENABLE_OC=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(( $(nproc) / 2 ))
```

**Prerequisites:** `sudo apt install llvm-14-dev`

## Git
```bash
git remote -v
# origin = AntelopeIO/spring (upstream)
# anvo = Anvo-Network/core (our fork)
# user.name=rwcii, user.email=rwcii@users.noreply.github.com
```
