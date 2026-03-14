# WASM Execution Layer - Threading Model Analysis

## Overview

The WASM execution layer is the **best-prepared component** for parallel execution.
Per-thread isolation is already largely implemented for the read-only transaction feature.
The main gaps are at the state access layer, not the VM itself.

## Runtime Architecture

Spring supports three WASM runtimes:

| Runtime | Type | Platform | Best For |
|---------|------|----------|----------|
| eos-vm | Interpreter | All | Development, non-x86 |
| eos-vm-jit | JIT compiler | x86_64 | General use |
| eos-vm-oc | LLVM AOT compiler | x86_64 Linux | Production (fastest) |

The `wasm_interface` class is a thin wrapper around `wasm_interface_impl`, which manages
runtime selection and the code cache.

### apply() Call Chain

```
wasm_interface::apply(code_hash, vm_type, vm_version, apply_context)
  → wasm_interface_impl::apply()
    → [OC path] eosvmoc_tier::exec->execute(code, mem, context)
    → [fallback] runtime_interface->instantiate_module()->apply(context)
```

## Per-Thread Resources (Already Isolated)

### eos-vm / eos-vm-jit

```cpp
// Each thread gets its own backend and execution context
thread_local static eosio::vm::wasm_allocator wasm_alloc;  // controller.cpp
```

Each `apply()` call sets the thread-local allocator on the backend:
```cpp
_runtime->_bkend.set_wasm_allocator(&context.control.get_wasm_allocator());
```

### eos-vm-oc (Optimized Compiler)

```cpp
struct eosvmoc_tier {
    eosvmoc::code_cache_async cc;                              // SHARED
    thread_local static std::unique_ptr<eosvmoc::executor> exec;  // PER-THREAD
    thread_local static std::unique_ptr<eosvmoc::memory> mem;     // PER-THREAD
};
```

**Per-thread executor** — handles WASM function dispatch, signal handling, x86-64 GS
segment setup. No sharing between threads.

**Per-thread memory** — WASM linear memory allocation with two layouts:
- Main thread: full allocation (8GB+ virtual address space)
- Read-only threads: sliced layout (10 pages, mprotect-expanded on demand)

### GS Segment Architecture (x86-64)

Each OC executor sets the x86-64 GS base register to point at a per-execution control block:

```cpp
struct eos_vm_oc_control_block {
    uint64_t magic;
    uintptr_t execution_thread_code_start;
    uintptr_t execution_thread_memory_start;
    apply_context* ctx;                          // per-execution context
    std::exception_ptr* eptr;
    unsigned current_call_depth_remaining;
    int64_t current_linear_memory_pages;
    char* full_linear_memory_start;
    sigjmp_buf* jmp;
    std::list<std::vector<std::byte>>* bounce_buffers;
    void* globals;
};
```

Host functions access `apply_context` via GS:
```cpp
apply_context* ctx;
asm("mov %%gs:%c[applyContextOffset], %[cPtr]\n" : [cPtr] "=r" (ctx));
```

This is **already thread-safe** — each thread has its own GS pointing to its own control block.

### Thread-Aware Module Instantiation

The OC instantiated module already branches on thread identity:
```cpp
void apply(apply_context& context) override {
    if (is_main_thread())
        _eosvmoc_runtime.exec.execute(*cd, _eosvmoc_runtime.mem, context);
    else
        _eosvmoc_runtime.exec_thread_local->execute(*cd, *_eosvmoc_runtime.mem_thread_local, context);
}
```

## Host Functions (Intrinsics)

### Registration & Dispatch

Intrinsics are registered at process startup via static constructors into an immutable
`intrinsic_map_t` (std::map). They're dispatched via a jump table embedded in the WASM
memory prologue, accessed through the GS segment.

**Thread-safe for reads** — the map never changes after initialization.

### Categorization by State Access

**Pure / Stateless (safe for parallel execution):**
- Memory ops: `memcpy`, `memmove`, `memset`, `memcmp`
- Math: `__divti3`, `__addtf3`, `__multf3`, etc.
- Type conversions: float/int operations
- Crypto: `sha256`, `sha512`, `ripemd160`, `recover_key`
- Assertions: `eosio_assert`, `eosio_assert_message`

**Read shared state (safe with snapshot isolation):**
- `current_block_time`, `get_active_producers`
- `get_blockchain_parameters_packed`
- `is_privileged`, `is_account`
- `db_find_i64`, `db_get_i64`, `db_next_i64`, secondary index lookups

**Write shared state (requires conflict tracking):**
- `db_store_i64`, `db_update_i64`, `db_remove_i64`
- Secondary index store/update/remove
- `set_resource_limits`, `set_proposed_producers`
- `send_inline`, `send_deferred` (spawn child actions/deferred transactions)
- `set_action_return_value`

**Context access mechanism:**
All stateful intrinsics go through `apply_context`, which provides the database handle
and transaction context. For parallel execution, each thread needs its own `apply_context`
pointing to an isolated state view.

## Code Cache

### code_cache_async (Production Mode)

Synchronization primitives:
- `std::mutex _mtx` — protects `_queued_compiles`, `_outstanding_compiles_and_poison`
- `boost::lockfree::queue _result_queue` — compile results from OOP compile process
- `std::atomic<size_t> _outstanding_compiles` — pending compilation counter
- `std::atomic<uint64_t> _executing_id` — currently executing code ID

**Flow:**
1. `get_descriptor_for_code()` — check cache, queue compile if missing
2. Compile happens out-of-process (separate binary)
3. Results arrive on `_result_queue`
4. Consumed during next `apply()` call on main thread

**Thread safety:** Mostly safe — mutex-protected mutations, lock-free queue for results.
The cache index itself (`_cache_index`, a multi_index_container) is accessed only during
write window today but would need read-locking for parallel write execution.

### Instantiation Cache

```cpp
mutable std::mutex instantiation_cache_mutex;
wasm_cache_index wasm_instantiation_cache;
```

Protected by mutex. Safe for concurrent access but could become a contention point
under heavy load with many different contracts.

## Thread Safety Summary

| Component | Thread-Safe? | Notes |
|-----------|-------------|-------|
| Executor (exec) | YES | thread_local per thread |
| Memory (mem) | YES | thread_local, sliced for non-main |
| Control Block | YES | Per-execution via GS segment |
| WASM Allocator | YES | thread_local in controller |
| Intrinsic Map | YES | Immutable after init |
| Intrinsic Dispatch | YES | Via per-thread GS segment |
| Module Instantiation | YES | Already branches on thread ID |
| Code Cache (reads) | YES | Mutex-protected |
| Code Cache (writes) | PARTIAL | Only safe during write window |
| Instantiation Cache | YES | Mutex-protected |

## What's Needed for Parallel Write Execution

The WASM layer itself is mostly ready. The gaps are:

1. **Per-thread apply_context** — each parallel execution needs its own context
   pointing to an isolated state view (shadow state or MVCC snapshot)

2. **Code cache read-safety** — currently optimized for single-writer; needs
   concurrent read access during parallel execution (likely just adding read locks)

3. **Memory allocation for write threads** — currently only main thread gets full
   memory allocation; parallel write threads would need the same (not the sliced
   RO layout)

4. **Iterator isolation** — the `keyval_cache` in apply_context tracks open
   iterators per-execution; already per-context so naturally isolated

## Conclusion

The WASM runtime is the **least problematic** component for parallel execution.
The per-thread executor/memory/allocator/GS-segment architecture was designed for
concurrent use (initially for read-only parallel transactions). Extending this to
write-parallel execution requires changes at the state layer (ChainBase), not the
VM layer. The WASM side essentially "just works" if you give each thread its own
`apply_context` with an isolated state view.
