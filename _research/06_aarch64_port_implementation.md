# AArch64 Port — Detailed Implementation Plan

**Status: COMPLETE** — All tests pass on AArch64. Merged to main. Component rebranded
to Core VM OC. See [20_core_vm_oc_architecture.md](20_core_vm_oc_architecture.md) for
the current architecture reference.

## Overview

This document provides the original file-by-file plan for porting the OC runtime from
x86_64 to AArch64 (ARM64), plus the bugs discovered and fixed during implementation.

> **Note on file paths:** This plan was written before the OC component rebrand.
> Directory paths referencing `eos-vm-oc/` now correspond to `core-vm-oc/` in the
> current codebase. Similarly, `eos-vm-oc.hpp` → `core-vm-oc.hpp`,
> `eos-vm-oc.h` → `core-vm-oc.h`. Code identifiers like `eosvmoc_switch_stack`
> are now `corevmoc_switch_stack`, etc. See [20_core_vm_oc_architecture.md](20_core_vm_oc_architecture.md)
> for current file paths.

## The Core Problem

The OC runtime (originally eos-vm-oc) uses the x86_64 **GS segment register** as a base pointer for all WASM
execution context. Every memory access, every host function call, every global variable
read goes through GS. This pattern is baked into three layers:

1. **C runtime code** — `gs_seg_helpers.c` reads/writes GS via `arch_prctl()` or FSGSBASE
2. **LLVM IR generation** — `LLVMEmitIR.cpp` emits pointers in address space 256 (GS encoding)
3. **Signal handler** — `executor.cpp` reads GS to find the control block on SIGSEGV

## AArch64 Replacement Strategy: TPIDR_EL0

AArch64 has `TPIDR_EL0` — a user-accessible thread-local register that serves a similar
purpose to GS. It's how pthreads implements TLS on ARM.

However, `TPIDR_EL0` is already used by the C library for thread-local storage. We have
two options:

### Option 1: Dedicated Register (Recommended)

Reserve a callee-saved register (e.g., **X28**) via `-ffixed-x28` compiler flag. The
register holds the memory base pointer during WASM execution, equivalent to GS on x86_64.

**Pros:**
- No conflict with TLS
- Faster than TPIDR_EL0 (register access vs system register access)
- Well-established pattern (V8, SpiderMonkey, Wasmtime all do this on ARM)

**Cons:**
- One fewer general-purpose register for the compiler
- Must ensure all compiled code (including LLVM output) respects the reservation
- All host functions called from WASM must also be compiled with `-ffixed-x28`

### Option 2: TPIDR_EL0 with Save/Restore

Save the C library's TPIDR_EL0 value on entry to WASM execution, set our memory base,
restore on exit.

**Pros:**
- No register reservation needed
- Matches the GS pattern more closely

**Cons:**
- Any callback to host code (intrinsics) needs TPIDR_EL0 restored for TLS access
- Error-prone — one missed restore corrupts the C runtime
- Signal handlers need careful TPIDR_EL0 management

**Recommendation: Option 1 (dedicated X28).** It's what other WASM runtimes do on ARM,
and it avoids the fragility of TPIDR_EL0 save/restore around every intrinsic call.

## File-by-File Changes

### 1. gs_seg_helpers.h → platform_helpers.h

**File:** `libraries/chain/include/core/chain/webassembly/eos-vm-oc/gs_seg_helpers.h`

**Current (x86_64):**
```c
#ifdef __clang__
#define GS_PTR __attribute__((address_space(256)))
#else
#define GS_PTR __seg_gs
#endif
#define EOS_VM_OC_CONTROL_BLOCK_OFFSET (-18944)
#define EOS_VM_OC_MEMORY_STRIDE (UINT64_C(8589963264))
```

**New (architecture-abstracted):**
```c
#if defined(__x86_64__) || defined(__amd64__)
   // Existing GS-based implementation
   #ifdef __clang__
   #define VMBASE_PTR __attribute__((address_space(256)))
   #else
   #define VMBASE_PTR __seg_gs
   #endif
   #define VMBASE_READ()  eos_vm_oc_getgs()
   #define VMBASE_WRITE(v) eos_vm_oc_setgs(v)

#elif defined(__aarch64__)
   // X28 holds memory base pointer
   #define VMBASE_PTR  // no special address space on ARM
   #define VMBASE_READ()  ({ uint64_t _v; asm("mov %0, x28" : "=r"(_v)); _v; })
   #define VMBASE_WRITE(v) asm("mov x28, %0" :: "r"((uint64_t)(v)))

#else
   #error "Unsupported architecture"
#endif

// These stay the same — they're not architecture-specific
#define EOS_VM_OC_CONTROL_BLOCK_OFFSET (-18944)
#define EOS_VM_OC_MEMORY_STRIDE (UINT64_C(8589963264))
```

**Complexity:** Low. Macro abstraction layer.

### 2. gs_seg_helpers.c → platform_helpers.c

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/gs_seg_helpers.c`

This file contains the C functions that access the control block via GS pointer.
The key macro `EOSVMOC_MEMORY_PTR_cb_ptr` creates a GS-based pointer to the control block.

**Current (x86_64):**
```c
#define EOSVMOC_MEMORY_PTR_cb_ptr \
   GS_PTR struct eos_vm_oc_control_block* const cb_ptr = \
   ((GS_PTR struct eos_vm_oc_control_block* const)(EOS_VM_OC_CONTROL_BLOCK_OFFSET))
```

**New (AArch64 path):**
```c
#if defined(__aarch64__)
#define EOSVMOC_MEMORY_PTR_cb_ptr \
   struct eos_vm_oc_control_block* const cb_ptr = \
   ((struct eos_vm_oc_control_block*)((char*)VMBASE_READ() + EOS_VM_OC_CONTROL_BLOCK_OFFSET))
#endif
```

On x86_64, `GS_PTR` tells the compiler to emit `%gs:` prefixed instructions automatically.
On AArch64, we explicitly compute `base_register + offset`. The compiler generates a
register-relative load, which is equally efficient.

**Functions to update:**
- `eos_vm_oc_grow_memory()` — accesses `cb_ptr->current_linear_memory_pages` etc.
- `eos_vm_oc_get_jmp_buf()` — accesses `cb_ptr->jmp`
- `eos_vm_oc_get_exception_ptr()` — accesses `cb_ptr->eptr`
- `eos_vm_oc_get_bounce_buffer_list()` — accesses `cb_ptr->bounce_buffers`

All use the same `EOSVMOC_MEMORY_PTR_cb_ptr` macro, so fixing the macro fixes all of them.

**Additionally remove on AArch64:**
- `arch_prctl()` calls (x86_64-only syscall)
- `_readgsbase_u64()` / `_writegsbase_u64()` (FSGSBASE instructions)
- `#include <immintrin.h>` (x86 intrinsics header)

**Complexity:** Medium. Straightforward but must be tested carefully.

### 3. LLVMEmitIR.cpp — The Big One

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/LLVMEmitIR.cpp`

This is the hardest file to port. It generates LLVM IR that uses **address space 256**
(x86_64 GS segment) for all memory accesses.

**Every `.getPointerTo(256)` call must be changed.**

There are ~15 occurrences at lines: 325, 366, 704, 792, 815, 820, 905, 1282, 1284,
1298, 1300-1301, and more.

**Strategy: Replace address space 256 with explicit base register load**

On x86_64, `address_space(256)` tells LLVM to emit `%gs:`-prefixed memory operations.
There is no equivalent address space for AArch64. Instead, we:

1. At function entry, load the base pointer from X28 into an LLVM SSA value
2. Use that value as the base for all GEP (GetElementPtr) operations
3. Use normal address space 0 pointers (default)

**Current (x86_64):**
```cpp
// Memory base is literal zero in address space 256
defaultMemoryBase = emitLiteralPointer(0, llvmI8Type->getPointerTo(256));

// Memory access
auto ptr = irBuilder.CreateGEP(defaultMemoryBase, index);
auto cast = irBuilder.CreatePointerCast(ptr, memoryType->getPointerTo(256));
auto load = irBuilder.CreateLoad(cast);
```

**New (AArch64):**
```cpp
// Memory base loaded from dedicated register (X28)
// Create inline asm to read X28
auto asmReadBase = llvm::InlineAsm::get(
    llvm::FunctionType::get(llvmI64Type, false),
    "mov $0, x28", "=r", /*hasSideEffects=*/true);
auto baseI64 = irBuilder.CreateCall(asmReadBase);
defaultMemoryBase = irBuilder.CreateIntToPtr(baseI64, llvmI8Type->getPointerTo(0));

// Memory access — same GEP but address space 0
auto ptr = irBuilder.CreateGEP(defaultMemoryBase, index);
auto cast = irBuilder.CreatePointerCast(ptr, memoryType->getPointerTo(0));
auto load = irBuilder.CreateLoad(cast);
```

**Alternatively**, use LLVM's target-specific intrinsic for reading a fixed register.
LLVM has `llvm.read_register` for this:

```cpp
auto regMD = llvm::MDNode::get(context, llvm::MDString::get(context, "x28"));
auto readReg = llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::read_register,
                                                 {llvmI64Type});
auto baseI64 = irBuilder.CreateCall(readReg, {llvm::MetadataAsValue::get(context, regMD)});
defaultMemoryBase = irBuilder.CreateIntToPtr(baseI64, llvmI8Type->getPointerTo(0));
```

This is cleaner and lets LLVM optimize the register read.

**Key decision:** Load the base register **once per function entry** and reuse the SSA
value. The compiler will keep it in a register throughout the function. On x86_64,
GS is implicitly available on every instruction; on AArch64, X28 is equally available
since it's a reserved register.

**Intrinsic dispatch also changes:**
```cpp
// Current: load function pointer from GS-relative jump table
auto ic = irBuilder.CreateLoad(
    emitLiteralPointer((void*)(OFFSET_OF_FIRST_INTRINSIC - ordinal*8),
                       llvmI64Type->getPointerTo(256)));

// New: load from base + offset
auto intrinsicAddr = irBuilder.CreateGEP(defaultMemoryBase,
    emitLiteral((I64)(OFFSET_OF_FIRST_INTRINSIC - ordinal*8)));
auto intrinsicPtr = irBuilder.CreatePointerCast(intrinsicAddr,
    llvmI64Type->getPointerTo(0));
auto ic = irBuilder.CreateLoad(intrinsicPtr);
```

**Complexity:** High. ~15 code locations to change, each carefully. Must produce
functionally identical behavior. Heavy testing required.

### 4. switch_stack_linux.s → switch_stack_linux_aarch64.s

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/switch_stack_linux.s`

**Current (x86_64, 7 instructions):**
```asm
eosvmoc_switch_stack:
    movq %rsp, -16(%rdi)
    leaq -16(%rdi), %rsp
    movq %rdx, %rdi
    callq *%rsi
    mov (%rsp), %rsp
    retq
```

**New (AArch64):**
```asm
// switch_stack_linux_aarch64.s
.global eosvmoc_switch_stack
.type eosvmoc_switch_stack, @function
eosvmoc_switch_stack:
    // x0 = new stack top, x1 = function pointer, x2 = argument
    mov x9, sp              // save current SP
    str x9, [x0, #-16]     // store old SP at top of new stack
    sub sp, x0, #16         // switch to new stack
    mov x0, x2              // argument → x0 (first arg register)
    blr x1                  // call function
    ldr x9, [sp]            // restore old SP
    mov sp, x9
    ret
```

**Complexity:** Low. ~1 day of work. Standard stack switching pattern.

### 5. executor.cpp — Signal Handler and Execution Setup

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/executor.cpp`

#### Signal Handler (segv_handler)

**Current:**
```cpp
static void segv_handler(int sig, siginfo_t* info, void* ctx) {
    uint64_t current_gs = eos_vm_oc_getgs();
    if (current_gs == 0) goto notus;
    control_block* cb = reinterpret_cast<control_block*>(current_gs - memory::cb_offset);
    ...
}
```

**New (AArch64):**
```cpp
static void segv_handler(int sig, siginfo_t* info, void* ucontext) {
    // On AArch64, read X28 from the signal context
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    uint64_t base_reg = uc->uc_mcontext.regs[28];  // X28
    if (base_reg == 0) goto notus;
    control_block* cb = reinterpret_cast<control_block*>(base_reg + EOS_VM_OC_CONTROL_BLOCK_OFFSET);
    ...
}
```

On x86_64, the signal handler calls `eos_vm_oc_getgs()` which is a syscall or FSGSBASE
instruction. On AArch64, we read X28 directly from the saved register context in the
`ucontext_t` — this is cheaper and more reliable.

#### Execution Setup

**Current:**
```cpp
eos_vm_oc_setgs((uint64_t)mem.zero_page_memory_base() + initial_page_offset * memory::stride);
```

**New:**
```cpp
VMBASE_WRITE((uint64_t)mem.zero_page_memory_base() + initial_page_offset * memory::stride);
```

Uses the macro from the abstracted `platform_helpers.h`.

#### Code Timeout via mprotect

**Current (line ~231):**
```cpp
syscall(SYS_mprotect, code_mapping, code_mapping_size, PROT_NONE);
```

This works identically on AArch64. No changes needed. SIGSEGV will fire when the
next instruction tries to execute from the now-protected code page, and the signal
handler catches it.

**Complexity:** Medium. Signal handler changes need careful testing.

### 6. memory.cpp / memory.hpp — Memory Layout

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/memory.cpp`
**File:** `libraries/chain/include/core/chain/webassembly/eos-vm-oc/memory.hpp`

#### Page Size Issue

The prologue size calculation hardcodes 4KB alignment:

```cpp
// memory.hpp, line 23
static constexpr uintptr_t memory_prologue_size =
    ((wcb_allowance + mutable_global_size + table_size + intrinsic_count*8) + 4095) / 4096 * 4096;
```

AArch64 Linux supports both 4KB and 64KB page sizes. With 64KB pages, `mprotect()`
calls must be 64KB-aligned.

**Fix:** Make prologue alignment runtime-detected:

```cpp
static const uintptr_t system_page_size = sysconf(_SC_PAGE_SIZE);
static const uintptr_t memory_prologue_size =
    ((wcb_allowance + mutable_global_size + table_size + intrinsic_count*8)
     + system_page_size - 1) / system_page_size * system_page_size;
```

**Note:** This changes `memory_prologue_size` from `constexpr` to a runtime value.
The stride calculation and other derived constants also need to become runtime values.
This is a somewhat invasive change but necessary for correctness on 64KB-page systems.

**Alternative:** Require 4KB pages on AArch64 Linux. AWS Graviton and most ARM Linux
distributions default to 4KB pages. 64KB pages are opt-in via kernel config. This
defers the problem but limits deployment flexibility.

#### Memory Stride

The stride (`8,589,963,264 bytes = ~8GB`) doesn't need to change for AArch64. AArch64
supports 48-bit virtual addresses (256TB) by default, same as x86_64. The stride is
well within bounds.

#### mmap Flags

`MAP_PRIVATE`, `MAP_ANON`, `MAP_SHARED`, `MAP_FIXED` — all work identically on AArch64
Linux. No changes needed.

**Complexity:** Low-Medium. Mostly mechanical, but the `constexpr` → runtime change
for page size alignment ripples through several constants.

### 7. LLVMJIT.cpp — Target Machine

**File:** `libraries/chain/webassembly/runtimes/eos-vm-oc/LLVMJIT.cpp`

**Current:**
```cpp
llvm::InitializeNativeTarget();  // Initializes x86_64 backend
auto targetTriple = llvm::sys::getProcessTriple();  // Returns x86_64 triple
```

**New:** No changes needed! `InitializeNativeTarget()` initializes whatever architecture
the code is compiled on. On AArch64 it initializes the AArch64 backend.
`getProcessTriple()` returns the AArch64 triple.

**One consideration:** The code model is set to `CodeModel::Small`. On AArch64 this
means code/data within 4GB, accessed via ADRP+ADD. This should be fine for WASM modules
but verify with large contracts.

**Also need to verify:** LLVM's AArch64 backend handles the PIC relocation model correctly
for our use case (loading compiled code at runtime via mmap).

**Complexity:** Low. Likely zero code changes, just verification.

### 8. compile_trampoline.cpp / compile_monitor.cpp

**Files:**
- `libraries/chain/webassembly/runtimes/eos-vm-oc/compile_trampoline.cpp`
- `libraries/chain/webassembly/runtimes/eos-vm-oc/compile_monitor.cpp`

These use `prctl()` for process naming and death signals, and `setrlimit()` for
resource limits. All POSIX/Linux APIs, not x86_64-specific.

**Complexity:** None. No changes needed.

### 9. memfd_helpers.hpp

**File:** `libraries/chain/include/core/chain/webassembly/eos-vm-oc/memfd_helpers.hpp`

Uses `memfd_create()` — Linux 3.17+, not architecture-specific.

**Complexity:** None. No changes needed.

### 10. CMakeLists.txt — Build System

**File:** `CMakeLists.txt` (root)

**Current:**
```cmake
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
    list(APPEND EOSIO_WASM_RUNTIMES eos-vm-oc)
```

**New:**
```cmake
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" OR
       "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
        list(APPEND EOSIO_WASM_RUNTIMES eos-vm-oc)
    endif()
endif()
```

Also update the eos-vm-jit guard:
```cmake
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64 OR
   CMAKE_SYSTEM_PROCESSOR STREQUAL amd64 OR
   CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
    list(APPEND EOSIO_WASM_RUNTIMES eos-vm eos-vm-jit)
```

**Note:** eos-vm-jit will still fail on AArch64 unless the eos-vm library adds an ARM
JIT backend. For the initial port, you could skip eos-vm-jit on AArch64 and rely solely
on eos-vm (interpreter) + eos-vm-oc (LLVM AOT). This is actually the better configuration
anyway — eos-vm-oc is faster than eos-vm-jit.

Add compile flag for dedicated register:
```cmake
if(CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
    add_compile_options(-ffixed-x28)
endif()
```

**Also add AArch64 assembly file:**
```cmake
if(CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
    target_sources(eos-vm-oc PRIVATE switch_stack_linux_aarch64.s)
else()
    target_sources(eos-vm-oc PRIVATE switch_stack_linux.s)
endif()
```

**Complexity:** Low.

### 11. secp256k1 — Crypto Library

**File:** `libraries/libfc/secp256k1/CMakeLists.txt`

**Current:**
```cmake
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    target_compile_definitions(secp256k1-internal INTERFACE USE_ASM_X86_64=1)
endif()
```

**New:** Remove or keep as-is. Without `USE_ASM_X86_64`, secp256k1 uses its portable C
implementation. It's ~20-30% slower but functionally identical.

For better performance, consider updating to upstream libsecp256k1 which has AArch64
optimizations.

**Complexity:** Low. Works without changes (falls back to C).

### 12. CRC32 — city.cpp

**File:** `libraries/libfc/src/crypto/city.cpp`

**Current:**
```cpp
#if defined(__SSE4_2__) && defined(__x86_64__)
#include <nmmintrin.h>
#define MM_CRC32_U64I _mm_crc32_u64
```

**Add AArch64 path:**
```cpp
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#define MM_CRC32_U64I __crc32cd
```

**Complexity:** Trivial.

### 13. pagemap_accessor.hpp — ChainBase

**File:** `libraries/chaindb/include/chainbase/pagemap_accessor.hpp`

**Current:**
```cpp
#if defined(__linux__) && defined(__x86_64__)
```

**New:**
```cpp
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
```

`/proc/self/pagemap` works on AArch64 Linux. The soft-dirty bit may behave differently
on some ARM kernels — needs testing.

**Complexity:** Low, but needs verification on target hardware.

## Implementation Order

### Week 1-2: Build System & Easy Wins
1. Update CMakeLists.txt for AArch64 detection
2. Add `-ffixed-x28` compile flag for AArch64
3. Fix CRC32 intrinsics (city.cpp)
4. Fix pagemap_accessor.hpp guard
5. Verify secp256k1 builds with C fallback
6. Write `switch_stack_linux_aarch64.s`
7. Verify eos-vm interpreter runs correctly on AArch64

### Week 3-4: Platform Abstraction Layer
8. Create `platform_helpers.h` with architecture-abstracted macros
9. Refactor `gs_seg_helpers.c` → `platform_helpers.c` with AArch64 X28 path
10. Update all callers of `eos_vm_oc_getgs/setgs` to use `VMBASE_READ/WRITE`

### Week 5-8: LLVM IR Generation (The Hard Part)
11. Modify `LLVMEmitIR.cpp`:
    - Replace all `getPointerTo(256)` with architecture-conditional code
    - On AArch64: use `llvm.read_register("x28")` + normal address space 0
    - On x86_64: keep existing address space 256 path
12. Test IR generation produces correct AArch64 code
13. Verify compiled WASM functions execute correctly

### Week 6-8: Signal Handler & Executor
14. Update `executor.cpp` signal handler for AArch64
    - Read X28 from `ucontext_t` instead of `eos_vm_oc_getgs()`
15. Update executor setup/teardown to use VMBASE macros
16. Test mprotect-based code timeout on AArch64

### Week 7-10: Memory Layout
17. Make `memory_prologue_size` runtime-computed (page size detection)
18. Update derived constants (stride, offsets)
19. Verify memory mapping works on 4KB-page AArch64
20. Test on 64KB-page systems if available

### Week 9-12: Integration & Testing
21. Full integration testing on Graviton instances
22. Run complete unit test suite on AArch64
23. Benchmark against x86_64 performance
24. Run existing EOSIO contract test suite
25. Stress testing with realistic workloads

## Bugs Discovered During Port (2026-03-15)

These are bugs in the ORCv2 JIT integration (Phase 1E) that were **latent on x86_64**
and only manifested on AArch64 because the AArch64 codegen exercises different code
paths (outlined helpers, different materialization timing).

### Bug 1: LLVMContext Ownership (Heap Corruption)

**Root cause:** `LLVMEmitIR.cpp` declared `LLVMContext` as a file-scope static object.
`LLVMJIT.cpp` wrapped `&llvmModule->getContext()` in a `unique_ptr` and handed it to
ORCv2's `ThreadSafeContext`. When ORCv2 moved the `ThreadSafeModule` during
`IRCompileLayer::emit`, the destructor of the moved-from TSM called `delete` on the
file-scope object → heap corruption → SIGABRT.

**Why it didn't crash on x86_64:** Unknown — likely different heap layout or the ORCv1
path (LLVM 7-11) didn't move the module in the same way. The bug existed in the Phase
1E ORCv2 code but was never triggered by x86_64 tests.

**Fix:** Heap-allocate the context: `llvm::LLVMContext& context = *new llvm::LLVMContext;`

### Bug 2: finalizeMemory() Return Value

**Root cause:** `UnitMemoryManager::finalizeMemory()` returned `true`. In LLVM's
`RTDyldMemoryManager` convention, `true` = error, `false` = success. The ORCv1 path
never checked this return value. ORCv2's `RTDyldObjectLinkingLayer` does check it and
reports "Failed to materialize" when it returns true.

**Fix:** Return `false`.

### Bug 3: .stack_sizes Count on AArch64

**Root cause:** LLVM's AArch64 backend generates outlined save/restore helper functions
(for callee-saved registers) as separate symbols. Each gets its own `.stack_sizes`
entry, inflating the count from 70 (number of WASM functions) to 225. The strict `==`
assertion caused `_exit(1)`.

**Fix:** Changed check from `num_found != defs.size()` to `num_found < defs.size()`.

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| LLVM AArch64 codegen bug | Low | High | Run extensive WASM test suite; LLVM AArch64 is mature |
| X28 reservation conflicts with system libs | Low | High | Test with all linked libraries; `-ffixed-x28` is well-understood |
| 64KB page size breaks memory layout | Medium | Medium | Initially target 4KB-page systems only |
| Performance regression vs x86_64 OC | Medium | Medium | AArch64 LLVM codegen is good; may need tuning |
| Signal handler timing differences | Low | Medium | SIGSEGV handling is POSIX-standard on AArch64 |
| LLVM version compatibility | Low | Low | Same LLVM versions work on both architectures |
| ORCv2 latent bugs from Phase 1E | **Realized** | High | Three bugs found and fixed. May be more. |
| emitVmemPointer/resolveVmemPtr pointer math | **Active** | High | Runtime SIGSEGV — under investigation |

## Expected Performance

Based on AArch64 LLVM code generation quality and comparable JIT engines:

| Metric | x86_64 (current) | AArch64 (expected) |
|--------|------------------|-------------------|
| WASM execution | 1.0x (baseline) | 0.85-1.05x |
| Compile time | 1.0x | 0.9-1.0x (Graviton4 is fast) |
| Memory overhead | 1.0x | 1.0x (same layout) |
| Intrinsic dispatch | 1.0x | 0.95-1.0x (register vs GS) |

AArch64 performance should be within 15% of x86_64 for WASM execution. Some workloads
may actually be faster due to Graviton's strong single-thread performance and ARM's
more regular instruction encoding.

## Deliverables

1. Architecture abstraction headers (`platform_helpers.h`)
2. AArch64 stack switching assembly
3. Modified LLVM IR generation with dual-architecture support
4. AArch64 signal handler
5. Runtime page size detection
6. Updated build system
7. CI pipeline for AArch64 (Graviton runners)
8. Performance benchmark suite comparing x86_64 vs AArch64
