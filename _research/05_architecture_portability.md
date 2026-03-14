# Architecture Portability Analysis — Beyond x86_64

## Current State

Spring currently requires x86_64 for production use. On other architectures, only the
WASM interpreter is available, which is roughly **10-30x slower** than the JIT/OC runtimes.
No EOSIO-based chain runs in production on anything other than x86_64.

### Runtime Availability by Platform

| Platform | eos-vm (Interpreter) | eos-vm-jit | eos-vm-oc |
|----------|---------------------|------------|-----------|
| Linux x86_64 | YES | YES | YES (requires LLVM) |
| Linux AArch64 | YES | NO | NO |
| macOS x86_64 | YES | NO | NO |
| macOS ARM (Apple Silicon) | YES | NO | NO |

The CMake logic is explicit about this (`CMakeLists.txt:62-79`):
```cmake
# OC: Linux x86_64 only
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
    list(APPEND EOSIO_WASM_RUNTIMES eos-vm-oc)

# JIT: x86_64/amd64 only
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64 OR CMAKE_SYSTEM_PROCESSOR STREQUAL amd64)
    list(APPEND EOSIO_WASM_RUNTIMES eos-vm eos-vm-jit)
else()
    list(APPEND EOSIO_WASM_RUNTIMES eos-vm)  # interpreter only
```

## Complete Inventory of x86_64 Dependencies

### HARD — Requires Rewrite/Port

#### 1. eos-vm-oc: GS Segment Register Architecture

**Files:** `gs_seg_helpers.c`, `gs_seg_helpers.h`

The entire eos-vm-oc runtime uses the x86_64 GS segment register as a base pointer for
per-execution context. Every host function call, every memory access check, every context
lookup goes through GS:

```c
// x86_64-only: read/write GS base via FSGSBASE instructions
_readgsbase_u64()
_writegsbase_u64()

// or via system call fallback
arch_prctl(ARCH_GET_GS, &gs)
arch_prctl(ARCH_SET_GS, gs)

// All intrinsics access apply_context via GS
asm("mov %%gs:%c[offset], %[ptr]" : [ptr] "=r" (ctx));
```

**AArch64 equivalent:** Thread-local storage via `TPIDR_EL0` register, or a dedicated
register reserved via `-ffixed-x28` compiler flag (common in JIT compilers targeting ARM).

#### 2. eos-vm-oc: Stack Switching Assembly

**File:** `switch_stack_linux.s`

Pure x86_64 assembly for switching between host and WASM execution stacks:

```asm
eosvmoc_switch_stack:
    movq %rsp, -16(%rdi)    # save stack pointer
    leaq -16(%rdi), %rsp    # switch to new stack
    movq %rdx, %rdi          # pass argument
    callq *%rsi              # call function
    mov (%rsp), %rsp         # restore stack
    retq
```

**AArch64 equivalent:** ~10 lines of AArch64 assembly using SP, X29 (FP), X30 (LR).
Straightforward to write.

#### 3. eos-vm-oc: LLVM Code Generation

**File:** `LLVMJIT.cpp`

Uses `llvm::InitializeNativeTarget()` which initializes x86_64 code generation.
The LLVM pipeline compiles WASM → LLVM IR → x86_64 machine code.

```cpp
auto targetTriple = llvm::sys::getProcessTriple();
targetMachine = llvm::EngineBuilder()
    .selectTarget(llvm::Triple(targetTriple), "", "", ...);
```

**AArch64 equivalent:** This actually "just works" if LLVM is built with AArch64 support.
`getProcessTriple()` returns the host architecture, and LLVM has excellent AArch64 code
generation. The hard part isn't LLVM — it's everything else in eos-vm-oc.

#### 4. eos-vm-oc: SIGSEGV-Based Bounds Checking

**File:** `executor.cpp:37-74`

Uses SIGSEGV signal handler to catch out-of-bounds WASM memory accesses. The handler
checks the faulting address against the execution memory range (located via GS register):

```cpp
static void segv_handler(int sig, siginfo_t* info, void* ctx) {
    uint64_t current_gs = eos_vm_oc_getgs();
    control_block* cb = reinterpret_cast<control_block*>(current_gs - memory::cb_offset);

    // Check if fault is in WASM memory region
    if ((uintptr_t)info->si_addr >= cb->execution_thread_memory_start && ...)
        siglongjmp(*cb->jmp, EOSVMOC_EXIT_SEGV);
}
```

**AArch64 equivalent:** SIGSEGV handling works identically on AArch64 Linux. The
architecture-specific part is the GS register lookup (see item 1). Replace GS with
the AArch64 equivalent and this works.

#### 5. eos-vm-jit: x86_64 JIT Backend in eos-vm

**File:** `libraries/eos-vm/` (external submodule)

The eos-vm library's JIT backend (`eosio::vm::jit`) emits x86_64 machine code directly.
There is no AArch64 JIT backend.

**AArch64 equivalent:** Would need a new JIT backend in eos-vm that emits AArch64
instructions. This is a significant effort (~5-10K lines of code for a basic backend).
Alternatively, rely on eos-vm-oc (via LLVM) which can target AArch64.

#### 6. secp256k1 Assembly Optimizations

**File:** `libraries/libfc/secp256k1/CMakeLists.txt`

```cmake
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    target_compile_definitions(secp256k1-internal INTERFACE USE_ASM_X86_64=1)
endif()
```

**AArch64 equivalent:** The upstream `libsecp256k1` project already has AArch64 support.
Merge upstream changes or use the C fallback (slower but functional).

#### 7. ChainBase Pagemap Accessor

**File:** `libraries/chaindb/include/chainbase/pagemap_accessor.hpp`

```cpp
#if defined(__linux__) && defined(__x86_64__)
// Uses /proc/self/pagemap for soft-dirty page tracking
```

**AArch64 equivalent:** `/proc/self/pagemap` works on AArch64 Linux, but the soft-dirty
bit tracking may differ. Needs testing. Alternative: use `userfaultfd` or `mprotect`-based
dirty tracking.

### MEDIUM — Has Fallback or Needs Conditional Compilation

#### 8. CRC32 Hardware Acceleration

**File:** `libraries/libfc/src/crypto/city.cpp`

```cpp
#if defined(__SSE4_2__) && defined(__x86_64__)
#include <nmmintrin.h>
#define MM_CRC32_U64I _mm_crc32_u64
#else
uint64_t mm_crc32_u64(uint64_t a, uint64_t b);  // portable fallback
```

**AArch64:** Has hardware CRC32 via `__ARM_FEATURE_CRC32` and `<arm_acle.h>`. Add a
third `#elif` branch. Trivial.

#### 9. xxhash Alignment Optimization

**File:** `libraries/wasm-jit/Source/ThirdParty/xxhash/xxhash.c`

x86 allows unaligned loads efficiently; ARM traditionally doesn't (though AArch64 handles
them well). The code already has the right conditional — just verify it works.

#### 10. Architecture Detection in ChainBase

**File:** `libraries/chaindb/include/chainbase/environment.hpp`

Already has multi-architecture detection:
```cpp
enum arch_t : unsigned char { ARCH_X86_64, ARCH_ARM, ARCH_RISCV, ARCH_OTHER };
```

This is fine — it prevents loading databases built on a different architecture.

### SOFT — Just Build System / CI Changes

- CI runner labels (`enf-x86-*` → add ARM runners)
- Package naming (`*-amd64.deb` → add `*-arm64.deb`)
- `BOOST_UNORDERED_DISABLE_NEON` compile flag (review whether still needed)

## The Key Question: Port eos-vm-oc or Replace It?

eos-vm-oc is ~80% of the x86_64 coupling. You have two strategic options:

### Option A: Port eos-vm-oc to AArch64

**What's needed:**
1. Replace GS segment → `TPIDR_EL0` or dedicated register (~2 weeks)
2. Rewrite stack switching assembly for AArch64 (~1 day)
3. Verify LLVM AArch64 code generation works with existing IR (~1 week)
4. Adapt memory layout constants for AArch64 page sizes (~1 week)
5. Test SIGSEGV handler on AArch64 (~1 week)
6. Port or replace eos-vm-jit with AArch64 backend (~2-3 months) OR drop it and use
   eos-vm-oc for both JIT tiers

**Total estimate:** 2-4 months for a developer familiar with AArch64 and JIT compilers.

**Pros:**
- Preserves the existing high-performance runtime
- LLVM already generates excellent AArch64 code
- AArch64 is a clean, modern ISA — in some ways easier than x86_64

**Cons:**
- Ongoing maintenance of two architecture backends
- GS → TPIDR_EL0 mapping isn't 1:1 (different performance characteristics)
- Need to handle Apple Silicon differences if targeting macOS

### Option B: Replace eos-vm-oc with a Portable High-Performance Runtime

> **Note:** This option was evaluated but NOT selected. The decision was made to
> port eos-vm-oc to AArch64 (Option A). See [06_aarch64_port_implementation.md](06_aarch64_port_implementation.md)
> for the implementation plan. This section is retained for reference.

Instead of porting the x86_64-specific runtime, replace it with a portable alternative:

**Candidates:**

| Runtime | Language | Performance vs OC | Portability | Maturity |
|---------|----------|-------------------|-------------|----------|
| **Wasmtime (Cranelift)** | Rust | ~0.8-0.9x | Excellent (x86_64, AArch64, s390x, RISC-V) | Production |
| **Wasmer (LLVM)** | Rust | ~0.9-1.0x | Good (x86_64, AArch64) | Production |
| **WAMR (Fast JIT)** | C | ~0.6-0.7x | Good (x86_64, AArch64, RISC-V) | Production |
| **V8 (Liftoff + TurboFan)** | C++ | ~1.0-1.1x | Excellent | Production |
| **Copy-and-Patch** | C | ~0.7-0.8x | Portable by design | Experimental |

**Wasmtime with Cranelift backend** was considered the strongest candidate:

- Cranelift is a portable code generator (x86_64, AArch64, s390x, RISC-V)
- Wasmtime has excellent WASI support and security track record
- ~10% slower than LLVM-based OC but compiles much faster (important for tierup latency)
- Already used in production by Fastly, Shopify, and others
- Bytecode Alliance backed (Mozilla, Fastly, Intel, Microsoft)

**Integration approach:**
1. Create a new runtime class implementing `wasm_runtime_interface`
2. Map Spring's host functions to Wasmtime's `Linker` API
3. Use Wasmtime's memory management instead of eos-vm-oc's custom mmap scheme
4. Keep eos-vm interpreter as fallback for maximum portability

**Total estimate:** 3-6 months. More work upfront but eliminates architecture-specific
maintenance forever.

### Option C: Hybrid — Port OC for AArch64, Plan Wasmtime for Long Term

Port eos-vm-oc to AArch64 now (it's the faster path to multi-arch support), while
building a Wasmtime integration as the long-term replacement for all architectures.
Eventually deprecate both eos-vm-jit and eos-vm-oc in favor of Wasmtime.

## Platform-Specific Considerations

### AArch64 Linux (AWS Graviton, Ampere Altra)

**Why this matters:** ARM servers are 20-40% cheaper per core-hour on AWS, Azure, and GCP.
Graviton3/4 instances offer excellent single-threaded performance. Many node operators
would benefit from ARM support.

**Specific considerations:**
- 4KB and 64KB page sizes (x86_64 is always 4KB) — memory layout code needs to handle both
- Weaker memory ordering than x86_64 — need explicit memory barriers in concurrent code
- No equivalent to x86_64 FSGSBASE — use `TPIDR_EL0` system register instead
- Different calling convention (X0-X7 for args vs RDI/RSI/RDX/RCX on x86_64)

### Apple Silicon (M1/M2/M3/M4)

**Additional challenges beyond AArch64:**
- macOS uses 16KB pages (not 4KB)
- JIT code requires `MAP_JIT` and `pthread_jit_write_protect_np()` toggling
- Hardened runtime restrictions on writable+executable memory
- Rosetta 2 can run x86_64 binaries but with performance penalty

### RISC-V (Future)

RISC-V servers are emerging (SiFive, StarFive). Not urgent but worth keeping the door open.
If you go with Wasmtime/Cranelift, RISC-V support comes for free.

## Recommendations for Your Fork

### Immediate (Step 1): AArch64 Linux Support — SELECTED

1. Fix the easy items: CRC32 intrinsics, secp256k1 fallback, build system changes
2. Verify the interpreter runs correctly on AArch64 (it should, but test it)
3. Port eos-vm-oc to AArch64:
   - Replace GS with TPIDR_EL0
   - Write AArch64 stack switch assembly
   - Verify LLVM AArch64 codegen
   - Handle 4KB/64KB page size differences
4. Drop eos-vm-jit requirement — let eos-vm-oc handle JIT on all platforms via LLVM

### Future Option (Step 2): Wasmtime Integration (NOT CURRENTLY PLANNED)

1. Build a `wasmtime_runtime` class implementing `wasm_runtime_interface`
2. Benchmark against eos-vm-oc on both x86_64 and AArch64
3. If performance is acceptable, make Wasmtime the default runtime
4. Keep eos-vm interpreter as emergency fallback

### Future Option (Step 3): Deprecate Architecture-Specific Code (NOT CURRENTLY PLANNED)

1. Remove eos-vm-oc and eos-vm-jit
2. Wasmtime becomes the only JIT runtime
3. All architecture support comes from Cranelift — x86_64, AArch64, RISC-V, s390x
4. Dramatically reduced maintenance burden

## Difficulty Assessment

| Task | Effort | Risk |
|------|--------|------|
| Build system changes for AArch64 | 1-2 days | Low |
| Crypto library portability (secp256k1, CRC32) | 1 week | Low |
| Interpreter verification on AArch64 | 1 week | Low |
| eos-vm-oc AArch64 port | 2-4 months | Medium |
| Wasmtime integration | 3-6 months | Medium |
| Full architecture-neutral runtime | 6-12 months | Medium-High |

**Bottom line:** Getting basic AArch64 support (interpreter-only) is trivial — maybe
two weeks of work. Getting *production-quality* AArch64 support with competitive
performance is a 2-6 month effort depending on the strategy chosen.
