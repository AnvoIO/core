# Block-STM Integration Plan for Spring

## What is Block-STM?

Block-STM (Software Transactional Memory) is a parallel execution engine developed by
Aptos. The core idea:

1. Execute all transactions in a block **optimistically in parallel**
2. Track what each transaction reads and writes (the read/write set)
3. If transaction B read something that transaction A wrote, and A hadn't committed yet,
   **abort B and re-execute** it after A commits
4. Most transactions don't conflict → near-linear speedup

Aptos reports 4-16x throughput improvement on real workloads.

## Why It Fits Spring

- **No protocol changes** — blocks look identical to sequential execution
- **Deterministic** — re-execution order is deterministic (based on tx index)
- **Graceful degradation** — worst case (all conflicts) = sequential performance
- **Most EOSIO workloads are low-conflict** — different users transacting with different
  contracts, different token balances, different NFTs

## Architecture Overview

```
                    Block (N transactions)
                           |
                    +------+------+
                    | Scheduler   |
                    | (tx order)  |
                    +------+------+
                           |
              +------------+------------+
              |            |            |
         Worker 0     Worker 1     Worker 2  ...
              |            |            |
         apply_ctx_0  apply_ctx_1  apply_ctx_2
              |            |            |
         shadow_db_0  shadow_db_1  shadow_db_2
              |            |            |
              +------------+------------+
                           |
                    Multi-Version
                    Data Structure
                           |
                    +------+------+
                    | Conflict    |
                    | Detector    |
                    +------+------+
                           |
                    Commit / Abort+Retry
```

## Implementation Phases

### Phase 0: Foundation — Multi-Version Data Structure

**The core data structure for Block-STM.**

Replace single-version ChainBase reads/writes with a multi-version store where each
transaction's writes are tagged with the transaction's index.

```cpp
// Conceptual multi-version wrapper
template<typename Key, typename Value>
class multi_version_data {
    // For each key, store writes indexed by transaction number
    struct version_entry {
        uint32_t tx_index;       // which transaction wrote this
        uint32_t incarnation;    // re-execution count
        Value value;
        bool is_estimate;        // true = written by aborted tx, needs re-read
    };

    // key → sorted list of version_entries (by tx_index)
    std::map<Key, std::vector<version_entry>> data;

    // Read: find latest version_entry where tx_index < reader's tx_index
    // Write: insert/update version_entry for writer's tx_index
};
```

**What needs multi-versioning:**

| State | Key Type | Conflict Frequency |
|-------|----------|-------------------|
| Contract rows | `(table_id, primary_key)` | Low |
| Secondary indices | `(table_id, secondary_key, primary_key)` | Low |
| Table metadata | `(code, scope, table)` | Medium (row count) |
| Account metadata | `account_name` | Medium (sequences, code_hash) |
| Resource usage | `account_name` | Low (different accounts) |
| Global properties | singleton | HIGH (global_sequence) |

### Phase 1: Defer Global Sequence Assignment

**The single highest-impact change.** Remove the `global_action_sequence` bottleneck.

Currently, every action calls `next_global_sequence()` which increments a global counter.
This creates a 100% conflict rate. The fix:

```cpp
// BEFORE (apply_context.cpp:1037-1045)
uint64_t apply_context::next_global_sequence() {
    const auto& p = control.get_dynamic_global_properties();
    db.modify(p, [&](auto& dgp) { ++dgp.global_action_sequence; });
    return p.global_action_sequence;
}

// AFTER — during parallel execution, don't assign yet
uint64_t apply_context::next_global_sequence() {
    if (parallel_mode()) {
        // Return placeholder; real value assigned in commit phase
        return DEFERRED_SEQUENCE;
    }
    // Original path for sequential execution
    const auto& p = control.get_dynamic_global_properties();
    db.modify(p, [&](auto& dgp) { ++dgp.global_action_sequence; });
    return p.global_action_sequence;
}
```

After all transactions execute, assign global sequences in transaction order:

```cpp
void assign_deferred_sequences(std::vector<transaction_trace>& traces) {
    uint64_t seq = starting_global_sequence;
    for (auto& trace : traces) {
        for (auto& action_trace : trace.action_traces) {
            action_trace.receipt->global_sequence = seq++;
        }
    }
    // Update dynamic_global_properties with final value
}
```

Similarly for `recv_sequence` and `auth_sequence` — accumulate per-account action counts
during parallel execution, assign actual values in order during commit.

### Phase 2: Shadow State Layer

Create a per-transaction state overlay that sits between the WASM execution and ChainBase.

```cpp
class transaction_shadow_state {
    uint32_t tx_index;
    uint32_t incarnation;

    // Read set: what this transaction read and from which version
    struct read_entry {
        StateKey key;
        uint32_t source_tx_index;    // which tx's write we read (or BASE for original)
        uint32_t source_incarnation;
    };
    std::vector<read_entry> read_set;

    // Write set: what this transaction wrote
    struct write_entry {
        StateKey key;
        Value value;
        enum op_t { CREATE, MODIFY, REMOVE } op;
    };
    std::vector<write_entry> write_set;
};
```

**Integration point:** Modify `apply_context`'s database operations to go through the
shadow state instead of directly to ChainBase:

```cpp
// BEFORE: apply_context directly modifies ChainBase
int apply_context::db_store_i64(...) {
    const auto& obj = db.create<key_value_object>(...);
    ...
}

// AFTER: apply_context writes to shadow state
int apply_context::db_store_i64(...) {
    if (parallel_mode()) {
        StateKey key{code, scope, table, primary_key};
        shadow_state->write(key, value, CREATE);
        return shadow_state->add_iterator(key);
    }
    // Original path
    const auto& obj = db.create<key_value_object>(...);
    ...
}
```

For reads, check shadow state first (own writes), then multi-version store (other txns'
committed writes), then ChainBase (base state):

```cpp
// Read path in shadow state
std::optional<Value> read(StateKey key) {
    // 1. Check own writes
    if (auto it = write_set.find(key); it != write_set.end())
        return it->value;

    // 2. Check multi-version store for latest committed write before our tx_index
    auto [value, source_tx, source_incarnation] = mvs.read(key, tx_index);
    read_set.push_back({key, source_tx, source_incarnation});

    if (value.is_estimate) {
        // Writer was aborted, we need to wait and retry
        throw DependencyException(source_tx);
    }

    return value;
}
```

### Phase 3: Scheduler & Conflict Detection

The Block-STM scheduler coordinates parallel execution:

```cpp
class block_stm_scheduler {
    uint32_t num_txns;
    uint32_t num_workers;

    // Per-transaction state
    enum class tx_status { PENDING, EXECUTING, EXECUTED, ABORTING, COMMITTED };
    std::vector<std::atomic<tx_status>> status;
    std::vector<uint32_t> incarnation;  // re-execution count

    // Work distribution
    std::atomic<uint32_t> execution_idx;   // next tx to execute
    std::atomic<uint32_t> validation_idx;  // next tx to validate

    void run() {
        // Each worker thread loops:
        while (true) {
            Task task = next_task();
            if (task.type == EXECUTE) {
                execute_transaction(task.tx_index);
            } else if (task.type == VALIDATE) {
                validate_transaction(task.tx_index);
            } else {
                break;  // all done
            }
        }
    }

    void validate_transaction(uint32_t tx_index) {
        // Check that everything this tx read is still valid
        for (auto& read : shadow_states[tx_index].read_set) {
            auto current = mvs.read(read.key, tx_index);
            if (current.source_tx != read.source_tx_index ||
                current.source_incarnation != read.source_incarnation) {
                // Read is stale — abort and re-execute
                abort_transaction(tx_index);
                return;
            }
        }
        // All reads valid — mark committed
        status[tx_index] = COMMITTED;
    }
};
```

### Phase 4: Integration with Controller

Modify `controller_impl` to use Block-STM when producing blocks:

```cpp
// controller.cpp — block production path
void push_transactions_parallel(std::vector<transaction_metadata_ptr>& txns) {
    block_stm_scheduler scheduler(txns.size(), thread_pool_size);

    // Create per-worker resources
    std::vector<std::unique_ptr<apply_context>> contexts(thread_pool_size);
    std::vector<std::unique_ptr<transaction_shadow_state>> shadows(txns.size());

    // Execute all transactions in parallel via Block-STM
    scheduler.run(txns, shadows, contexts);

    // All transactions now committed in order
    // Apply shadow states to ChainBase in transaction order
    for (uint32_t i = 0; i < txns.size(); i++) {
        apply_shadow_to_chainbase(shadows[i]);
    }

    // Assign deferred sequences
    assign_deferred_sequences(traces);
}
```

### Phase 5: Validation Node Support

Validating nodes (non-producers) also need parallel execution to keep up with faster
block production. The same Block-STM engine works: transactions are already ordered
in the block, execute them in parallel with the same conflict detection.

## Handling Specific Challenges

### Inline Actions & Notifications

Inline actions are spawned dynamically during execution. They execute within the same
transaction context, so they share the transaction's shadow state. No special handling
needed — they're part of the same read/write set.

### Deferred Transactions

Deferred transaction scheduling creates a `generated_transaction_object`. The key is
`(sender, sender_id)`. Two transactions scheduling deferred txns with different senders
don't conflict. Same sender + same sender_id = conflict, handled by normal MVS.

### RAM Accounting

RAM usage changes are tracked per-account. They're part of the write set (keyed by
account name). Conflicts occur when two transactions modify the same account's RAM
usage — detected and resolved by Block-STM retry.

### Resource Billing

CPU/NET billing happens in `finalize()`. Like sequences, these can be deferred:
accumulate per-transaction billing during parallel execution, apply to the resource
limits manager in order during the commit phase.

## Modified Files

### Core Changes

| File | Change |
|------|--------|
| `libraries/chain/controller.cpp` | Add parallel execution path in block production |
| `libraries/chain/transaction_context.cpp` | Support shadow state undo sessions |
| `libraries/chain/apply_context.cpp` | Route db ops through shadow state in parallel mode |
| `libraries/chain/include/core/chain/apply_context.hpp` | Add shadow state member |

### New Files

| File | Purpose |
|------|---------|
| `libraries/chain/include/core/chain/block_stm/scheduler.hpp` | Transaction scheduler |
| `libraries/chain/include/core/chain/block_stm/multi_version_store.hpp` | MVS data structure |
| `libraries/chain/include/core/chain/block_stm/shadow_state.hpp` | Per-tx state overlay |
| `libraries/chain/include/core/chain/block_stm/conflict_detector.hpp` | Read/write set validation |
| `libraries/chain/block_stm/scheduler.cpp` | Scheduler implementation |
| `libraries/chain/block_stm/multi_version_store.cpp` | MVS implementation |

### Unchanged

| Component | Why |
|-----------|-----|
| ChainBase | Used as base state only; not modified for parallel access |
| WASM runtimes | Already thread-safe with per-thread resources |
| Consensus/finality | Block format unchanged; same receipts, same Merkle roots |
| P2P networking | No changes needed |
| API plugins | No changes needed |

## Performance Expectations

### Best Case (Low Conflict)
- Token transfers between different accounts
- NFT mints to different collections
- Independent contract calls
- **Expected speedup: 4-8x** with 8 worker threads

### Moderate Conflict
- DEX trades on same market
- Popular game contract with shared state
- **Expected speedup: 2-4x** (some re-executions)

### Worst Case (High Conflict)
- All transactions modifying same contract row
- Sequential dependency chains
- **Expected speedup: ~1x** (degrades to sequential, with overhead)

### Aptos Benchmarks (Reference)
Aptos reports on their Block-STM paper:
- Coin transfers: ~16x speedup with 32 threads
- NFT minting: ~10x speedup
- Uniswap-like DEX: ~4x speedup
- Fully sequential workload: ~0.9x (small overhead)

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Overhead in low-parallelism workloads | Feature flag to fall back to sequential |
| Memory pressure from shadow states | Limit shadow state size; flush large txns sequentially |
| Correctness bugs in conflict detection | Extensive testing against sequential execution (compare receipts) |
| Determinism across nodes | Block-STM is deterministic by design; verify with integration tests |
| ChainBase undo stack interaction | Shadow states commit to ChainBase in order; undo stack sees sequential writes |

## Implementation Order

1. **Multi-version data structure** — can be developed and tested independently
2. **Shadow state layer** — wraps apply_context db operations; testable in isolation
3. **Deferred sequence assignment** — protocol-compatible change to receipt generation
4. **Scheduler** — orchestrates parallel execution with workers
5. **Controller integration** — wire it all together
6. **Performance tuning** — thread count, batch sizes, contention optimization
7. **Validation mode** — parallel block validation for non-producing nodes

Estimated effort: 3-6 months for a small team (2-3 experienced C++ developers),
assuming familiarity with the codebase.
