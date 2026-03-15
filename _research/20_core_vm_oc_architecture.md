# Core VM OC — Architecture & Implementation

## Overview

Core VM OC (Optimized Compiler) is the JIT compilation tier for WASM smart contract
execution. It compiles WASM bytecode into native machine code (x86_64 or AArch64) at
runtime, providing significant performance improvements over the eos-vm interpreter.

Core VM OC is **purely a local node optimization** — it is not part of chain state,
not part of consensus, and not visible to smart contracts or external APIs. Nodes can
run with or without OC enabled and produce identical chain state.

### Execution Tiers

| Tier | Runtime | Speed | When Used |
|------|---------|-------|-----------|
| Interpreter | `core_net::vm` (eos-vm) | 1x baseline | Always available, fallback |
| JIT | `core_net::vm` (eos-vm-jit) | ~3-5x | x86_64 only, legacy |
| OC | `core_net::chain::corevmoc` | ~10-50x | x86_64 + AArch64, tier-up |

OC tier-up is transparent: when a contract is first called, it executes in the
interpreter while OC compiles it in the background. On subsequent calls, the OC-compiled
native code is used. If OC compilation fails, the interpreter continues to work.

### Architecture Diagrams

```
Node Process (core_netd)
├── Chain Thread
│   ├── WASM Execution
│   │   ├── Interpreter (eos-vm) — always available
│   │   └── OC Native Code — if compiled and cached
│   └── Code Cache
│       ├── Memory-mapped code cache file
│       └── In-memory descriptor index
│
├── Compile Monitor Process (forked at startup)
│   ├── Receives compile requests via Unix socket
│   └── Compile Trampoline (forked per request)
│       └── Compile Child (forked per WASM module)
│           ├── WASM parsing + injection
│           ├── LLVM IR generation
│           ├── LLVM compilation (ORCv2)
│           └── Returns native code via memfd
```

## Code Cache

### File Format

The OC code cache is a memory-mapped file stored in the node's data directory at
`<data-dir>/code_cache/code_cache.bin`. It contains compiled native code for all
contracts the node has encountered.

**Header (at offset 512, size 512):**

```
struct code_cache_header {
   uint64_t id;                         // Magic identifier
   bool dirty;                          // Unclean shutdown flag
   uintptr_t serialized_descriptor_index; // Offset to descriptor table
};
```

**Magic identifiers:**

| Magic | Hex (little-endian) | Version | Context |
|-------|---------------------|---------|---------|
| `COREVMOC` | `0x434F4D5645524F43` | Current | New chains and upgraded nodes |
| `EOSVMOC2` | `0x32434F4D56534F45` | Legacy | Accepted on read for migration |

The magic identifier validates that the cache file belongs to this software. New caches
are always written with the `COREVMOC` magic. Legacy `EOSVMOC2` caches from Spring/EOSIO
nodes are accepted on read — the node will rebuild the cache transparently.

**Codegen version** (stored per code entry, not in the header):

```cpp
static constexpr uint8_t current_codegen_version = 3;
```

Each cached code entry records which codegen version produced it. When the node starts
and finds entries compiled with an older version, it discards them and recompiles from
the WASM bytecode already stored in chain state. This is transparent — no operator or
developer action required.

| Codegen Version | Changes |
|-----------------|---------|
| 2 | Spring/EOSIO baseline (ORCv1 on LLVM 7-11) |
| 3 | Core VM OC: ORCv2, AArch64 support, renamed intrinsics |

### Cache Lifecycle

1. **Creation:** Cache file is created on first node start with OC enabled
2. **Population:** Contracts are compiled on-demand as they're executed
3. **Persistence:** Cache survives node restarts (unless codegen version changes)
4. **Invalidation:** Codegen version bump → all entries recompiled on demand
5. **Migration:** Legacy `EOSVMOC2` cache → accepted, entries rebuilt with version 3
6. **Eviction:** LRU eviction when cache reaches `--core-vm-oc-cache-size-mb` limit

### Code Cache Allocator

The code cache uses a `boost::interprocess::rbtree_best_fit` allocator within the
memory-mapped file. Allocation alignment differs by architecture:

| Architecture | Alignment | Reason |
|-------------|-----------|--------|
| x86_64 | 16 bytes (`alignof(std::max_align_t)`) | RIP-relative addressing is byte-exact |
| AArch64 | 4096 bytes (page) | ADRP+ADD/LDR uses page-relative offsets |

The AArch64 page alignment is required because LLVM's AArch64 backend generates
ADRP instructions for internal data references (constant pools, literal pools, table
data). ADRP encodes page-relative offsets that are only correct when the code blob is
loaded at the same 4KB page alignment as where it was compiled.

## Compilation Pipeline

### Process Architecture

OC compilation runs in **isolated forked processes** to prevent compiler crashes from
affecting the node:

```
core_netd
  └── fork() → Compile Monitor (oc-monitor)
        └── fork() → Compile Trampoline (oc-trampoline)
              └── fork() → Compile Child (oc-compile, one per request)
                    ├── Deserialize WASM
                    ├── Inject softfloat calls
                    ├── Generate LLVM IR
                    ├── Compile via ORCv2
                    ├── Fill indirect call table
                    ├── Send result via memfd + Unix socket
                    └── _exit(0)
```

Each compile child is a fresh fork that processes exactly one WASM module and exits.
This provides:
- **Crash isolation:** LLVM bugs don't crash the node
- **Memory isolation:** Compiler memory is reclaimed on exit
- **Resource limits:** CPU and VM limits via `setrlimit()`

### WASM Injection

Before LLVM compilation, the WASM binary is processed by the injection pass
(`wasm_injection.hpp`). This replaces all floating-point opcodes with calls to
softfloat intrinsic functions:

```
Original WASM:     f64.add
After injection:   call $_core_net_f64_add
```

This ensures **deterministic floating-point behavior** across architectures. The
softfloat library (Berkeley SoftFloat 3) performs all FP arithmetic using integer
operations, producing bit-identical results on x86_64 and AArch64.

**Injected operations:** All f32/f64 arithmetic (`add`, `sub`, `mul`, `div`, `sqrt`,
`min`, `max`), comparisons (`eq`, `ne`, `lt`, `le`, `gt`, `ge`), conversions
(`promote`, `demote`, `trunc_s/u`, `convert_s/u`), and bitwise ops (`abs`, `neg`,
`copysign`, `ceil`, `floor`, `trunc`, `nearest`).

### LLVM IR Generation (LLVMEmitIR.cpp)

The IR generator translates injected WASM into LLVM IR with architecture-specific
memory addressing:

**x86_64:** Uses GS segment register (address space 256) for WASM memory base. The
hardware provides `%gs:` prefixed memory operations that automatically add the GS base
to every address.

**AArch64:** Uses dedicated register X28 (`-ffixed-x28`) as WASM memory base. At each
function entry, X28 is read via `llvm.read_register("x28")` and all WASM memory
accesses are computed as `X28 + offset`.

| Concept | x86_64 | AArch64 |
|---------|--------|---------|
| Memory base | GS segment register | X28 register |
| Address space | 256 (GS encoding) | 0 (default) |
| Pointer creation | `inttoptr(offset) in AS 256` | `inttoptr(X28 + offset) in AS 0` |
| Reservation | Kernel sets GS base | `-ffixed-x28` + `+reserve-x28` |
| Indirect call table | ADRP+ADD global (PIC) | Control block via X28 |

### LLVM JIT Compilation (LLVMJIT.cpp)

Uses LLVM ORCv2 JIT infrastructure (LLVM 12+) with backward compatibility for
LLVM 7-11 (ORCv1):

- **ORCv2 path:** `ThreadSafeModule` → `IRCompileLayer` → `RTDyldObjectLinkingLayer`
- **Optimization:** PromoteMemoryToRegister, InstructionCombining, CFGSimplification,
  JumpThreading, SCCP
- **Code model:** `Reloc::PIC_`, `CodeModel::Small`
- **AArch64 target features:** `+reserve-x28`

### Intrinsic Jump Table

Host functions (softfloat, memory growth, traps) are called through an **intrinsic
jump table** stored in the control block area, accessible via the memory base register
(GS on x86_64, X28 on AArch64).

The jump table is an array of function pointers at negative offsets from the control
block. Each intrinsic has a fixed ordinal (position) defined in
`intrinsic_mapping.hpp`. The JIT code loads the function pointer from
`base + OFFSET_OF_FIRST_INTRINSIC - ordinal * 8` and calls it.

## AArch64 Port

### Register Strategy

Register X28 is reserved as the WASM memory base pointer via:
- `-ffixed-x28` compiler flag (all project code)
- `+reserve-x28` LLVM target feature (JIT-generated code)

This is the standard approach used by V8, SpiderMonkey, Wasmtime, and DynamoRIO
on AArch64. X28 was chosen because:
- It's a callee-saved register (preserved across function calls)
- It's not used by the C library or system calls
- One register loss from the compiler's pool has minimal performance impact

### ADRP Page Alignment

AArch64's ADRP instruction computes addresses at 4KB page granularity:
```
result = (PC & ~0xFFF) + (imm21 << 12)
```

This means ADRP-based references (constant pools, literal pools, data sections) are
only correct when the code blob is loaded at the **same 4KB page alignment** as where
it was compiled. The OC pipeline ensures this by:

1. **Compilation buffer:** Allocated via `posix_memalign(4096)` — page-aligned
2. **Code cache allocator:** Uses 4096-byte minimum alignment on AArch64

On x86_64, RIP-relative addressing is byte-exact, so no alignment constraint exists.

### Signal Handling

The WASM execution timeout mechanism uses `mprotect()` to make code pages
non-executable, triggering SIGSEGV. The signal handler reads the memory base register
to find the control block:

- **x86_64:** Calls `arch_prctl(ARCH_GET_GS)` or reads via FSGSBASE instruction
- **AArch64:** Reads `ucontext->uc_mcontext.regs[28]` (X28 from saved context)

### Stack Switching

OC execution uses a separate stack for WASM code. Stack switching is implemented in
architecture-specific assembly:

- **x86_64:** `switch_stack_linux.s` — 7 instructions
- **AArch64:** `switch_stack_linux_aarch64.s` — saves callee-saved registers per
  AAPCS64, switches SP, calls function, restores

## Configuration

### CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--core-vm-oc-enable` | `auto` | Enable OC tier-up: `auto`, `all`, `none` |
| `--core-vm-oc-cache-size-mb` | 1024 | Maximum code cache size in MiB |
| `--core-vm-oc-compile-threads` | 1 | Number of compilation threads |
| `--core-vm-oc-whitelist` | `xsat,vaulta` | Account suffixes for tier-up in `auto` mode |

**Legacy aliases** (for migrating chains): `--eos-vm-oc-enable`, `--eos-vm-oc-cache-size-mb`,
`--eos-vm-oc-compile-threads`, `--eos-vm-oc-whitelist`. These map to the same options
and will continue to work indefinitely.

### Tier-Up Modes

| Mode | Behavior |
|------|----------|
| `auto` | OC for whitelisted accounts, read-only trxs, and non-producing block application |
| `all` | OC for all contract execution |
| `none` | OC disabled, interpreter only |

## Migration Guide

### From Spring/EOSIO Nodes

1. **Replace binary:** Install the new `core_netd` binary
2. **Config options:** Existing `--eos-vm-oc-*` options continue to work unchanged
3. **Code cache:** Existing cache is accepted and entries are rebuilt transparently
4. **No contract changes:** All deployed WASM contracts work unmodified

The only visible effect during migration is a brief period of slower contract execution
while the OC cache rebuilds in the background. The interpreter handles all execution
during this period.

### Cache Rebuild Triggers

The code cache is rebuilt when:
- Node is upgraded and codegen version changes (2→3 for this release)
- Cache file is deleted manually
- Cache file is corrupted (dirty shutdown without recovery)
- Contract WASM bytecode changes (new `set_code` action)

Rebuild is automatic and transparent — no operator action required.

## File Index

### Headers (`libraries/chain/include/core_net/chain/webassembly/core-vm-oc/`)

| File | Purpose |
|------|---------|
| `core-vm-oc.h` | C-level control block struct, exit codes |
| `core-vm-oc.hpp` | Code descriptor, codegen version, FC reflection |
| `code_cache.hpp` | Code cache class, allocator type |
| `config.hpp` | Compile limits, subjective configuration |
| `executor.hpp` | Executor class (runs compiled code) |
| `gs_seg_helpers.h` | Architecture-abstracted memory base macros |
| `intrinsic.hpp` | Intrinsic registration infrastructure |
| `intrinsic_mapping.hpp` | Intrinsic name→ordinal jump table |
| `ipc_protocol.hpp` | IPC messages between node and compile processes |
| `ipc_helpers.hpp` | IPC serialization helpers |
| `memfd_helpers.hpp` | memfd_create helpers for code transfer |
| `memory.hpp` | Memory layout constants (stride, prologue, offsets) |
| `stack.hpp` | Execution stack management |
| `compile_monitor.hpp` | Compile monitor process entry point |
| `compile_trampoline.hpp` | Compile trampoline process entry point |

### Sources (`libraries/chain/webassembly/runtimes/core-vm-oc/`)

| File | Purpose |
|------|---------|
| `LLVMEmitIR.cpp` | WASM → LLVM IR translation |
| `LLVMJIT.cpp` | LLVM IR → native code compilation (ORCv2) |
| `code_cache.cpp` | Code cache management, file I/O |
| `compile_monitor.cpp` | Compile monitor process |
| `compile_trampoline.cpp` | Compile trampoline + child fork |
| `executor.cpp` | Native code execution, signal handling |
| `gs_seg_helpers.c` | GS/X28 register access functions |
| `intrinsic.cpp` | Intrinsic map management |
| `ipc_helpers.cpp` | IPC message serialization |
| `memory.cpp` | Memory region setup, mmap |
| `stack.cpp` | Execution stack allocation |
| `switch_stack_linux.s` | x86_64 stack switching assembly |
| `switch_stack_linux_aarch64.s` | AArch64 stack switching assembly |

### Tests (`unittests/`)

| File | Tests |
|------|-------|
| `corevmoc_platform_tests.cpp` | Register r/w, memory constants, smoke, multi-contract |
| `corevmoc_limits_tests.cpp` | Compile limits, cache size, thread config |
| `corevmoc_interrupt_tests.cpp` | Execution timeout, signal handling |
