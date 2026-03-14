# ChainBase Internals - Deep Analysis

## Overview

ChainBase is Spring's state database — a memory-mapped, single-writer key-value store with a
deep undo stack. It's the foundation all chain state sits on, and the single biggest obstacle
to parallel transaction execution.

## Memory Model

### Storage: Memory-Mapped Files

ChainBase uses `boost::interprocess::managed_mapped_file` with four configurable mapping modes
(defined in `pinnable_mapped_file.hpp:52-57`):

| Mode | Description |
|------|-------------|
| `mapped` | `MAP_SHARED` — default, supports inter-process reads |
| `mapped_private` | `MAP_PRIVATE` — flushed only at exit |
| `heap` | Copied to anonymous mmap with 2MB huge pages at startup |
| `locked` | Same as heap but `mlock()`'d in RAM |

Files are sized to 1MB multiples. In `heap` mode, the entire file is copied in 1GB chunks
to an anonymous mapping at startup.

### Allocators

Two custom allocators:

1. **`chainbase_node_allocator<T>`** (`chainbase_node_allocator.hpp`):
   - Per-type freelist with batch size of 64
   - Returns `bip::offset_ptr<T>` for mmap portability
   - Single allocations use freelist; bulk go to segment_manager

2. **Copy-On-Write containers** for dynamic fields:
   - `shared_cow_string` — reference-counted (uint32_t refcount + size header)
   - `shared_cow_vector<T>` — same COW pattern
   - Same-segment copy = refcount++; cross-segment = deep copy

### Offset Pointers

All internal pointers use `bip::offset_ptr<T>` (relative offsets, not absolute addresses).
A 2-bit shift trick on the `offset_node_base` extends addressable space from 2TB to 8TB.
Special value `1` = nullptr (safe because minimum node size is 16 bytes).

## The Undo/Session System

This is the heart of ChainBase and the core barrier to parallelism.

### Data Structures

```cpp
// Per-index undo state (undo_index.hpp:300-350)
struct undo_state {
    pointer old_values_end;      // snapshot of old_values list position
    pointer removed_values_end;  // snapshot of removed_values list position
    id_type old_next_id;         // snapshot of ID counter
    uint64_t ctime;              // monotonic_revision at session creation
};
```

Three tracking mechanisms per index:

| Counter | Purpose | Scope |
|---------|---------|-------|
| `_revision` | Logical undo depth = undo_stack.size() | Per-index |
| `_monotonic_revision` | Timestamp for all undo-able operations | Per-index, monotonic |
| `_next_id` | Sequential object ID generator | Per-index |

### How Operations Are Tracked

**CREATE** (`emplace()`, lines 353-372):
- Assigns `obj.id = _next_id++`
- No undo entry — on undo, all IDs >= `undo_state.old_next_id` are erased

**MODIFY** (`modify()`, lines 378-406):
- If undo active and `node._mtime < undo_state.ctime`:
  - Allocates backup copy in `_old_values` linked list
  - Updates `node._mtime = ++_monotonic_revision`
- Applies modifier lambda, re-balances indices if key order changed

**REMOVE** (`remove()`, lines 408-414):
- If undo active and object existed before session:
  - Moves node from primary index to `_removed_values` linked list
  - Sets `removed_flag = erased_flag` (-2)

### Session Lifecycle

```
start_undo_session(true)
  → for each index: push undo_state to deque
  → save (old_values_end, removed_values_end, old_next_id, ctime)
  → increment _revision and _monotonic_revision

... modifications tracked in _old_values and _removed_values ...

push()    → discard undo info, keep changes (commit)
squash()  → merge top two sessions, compress redundant entries
undo()    → erase new IDs, restore old values, re-insert removed values
```

### Session Nesting

Sessions nest via move semantics. Only the outermost session's destructor triggers
undo if not explicitly pushed. This is how transaction-level rollback works within
block-level sessions: the block session is the outer session, each transaction creates
an inner session that squashes on success or undoes on failure.

## Index System

### Boost multi_index → undo_index

ChainBase converts standard `boost::multi_index_container` types into its own
`undo_index<T, Alloc, Indices...>` via template metaprogramming:

```cpp
template<typename MultiIndexType>
using generic_index = multi_index_to_undo_index<MultiIndexType>;
```

Requirements: first index must be `ordered_unique` on primary key.

### Node Layout

```cpp
struct node : hook<Indices>..., value_holder<T> {
    T _item;           // the actual data object
    uint64_t _mtime;   // modification timestamp for undo
};
```

Multiple inheritance: one AVL tree hook per index, plus the value. This is intrusive —
no separate node allocations.

### Registered Indices in Spring

**System indices** (controller_index_set):
- `account_index`, `account_metadata_index`, `account_ram_correction_index`
- `global_property_multi_index`, `protocol_state_multi_index`
- `dynamic_global_property_multi_index`, `block_summary_multi_index`
- `transaction_multi_index`, `generated_transaction_multi_index`
- `table_id_multi_index`, `code_index`, `database_header_multi_index`

**Contract data indices** (contract_database_index_set):
- `key_value_index` — primary contract table storage
- `index64_index`, `index128_index`, `index256_index`
- `index_double_index`, `index_long_double_index`

## Locking (or Lack Thereof)

ChainBase defines a `read_write_mutex_manager` with `CHAINBASE_NUM_RW_LOCKS` (default 10)
interprocess sharable mutexes. However:

**These locks are NOT used in the core database code.**

The actual concurrency model is:
- Multiple readers allowed (no locking needed for reads)
- ALL writes must be externally synchronized
- The database is fundamentally **single-writer**

## Global Mutable State Points

These are the specific points that prevent concurrent writes:

1. **`_monotonic_revision`** — incremented on every session start, read on every modify/remove
2. **`_undo_stack`** — shared deque, non-thread-safe operations
3. **`_next_id`** — sequential increment without atomics
4. **`_old_values` / `_removed_values`** — shared linked lists modified on every write
5. **Static `_instance_tracker`** — vector of all mapped file instances
6. **Static `_segment_manager_map`** — lookup table for allocator identification

## What This Means for Parallel Execution

### Cannot Be Fixed Without Redesign

1. **Single undo stack per index** — all sessions share one deque. Concurrent sessions
   would corrupt the undo history.

2. **Sequential ID generation** — `_next_id++` is not atomic. Concurrent creates
   produce duplicate IDs.

3. **Monotonic revision as implicit lock** — `_mtime` comparisons assume single-writer
   ordering. Concurrent writers produce incorrect undo tracking.

4. **No transaction isolation** — readers see partial writes (mid-modify state).
   No snapshot isolation between sessions.

### Potential Approaches

1. **MVCC (Multi-Version Concurrency Control)**
   - Each transaction gets a snapshot version
   - Writes create new versions, don't modify in place
   - Conflict detection at commit time
   - Requires fundamental redesign of node storage

2. **Per-thread shadow state with merge**
   - Each worker thread gets a copy-on-write overlay
   - Writes go to thread-local overlay
   - Overlays merged into main state after validation
   - Conflicts detected during merge

3. **Replace ChainBase entirely**
   - Purpose-built concurrent state store
   - e.g., LMDB with MVCC, RocksDB with optimistic transactions
   - Largest engineering effort but cleanest result

4. **Partition by scope**
   - Separate ChainBase instance per (code, scope) pair
   - Transactions touching different scopes use different instances
   - Cross-scope transactions still serialize
   - Moderate effort, natural fit for EOSIO's table model
