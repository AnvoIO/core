# Transaction Execution Pipeline - Deep Analysis

## Overview

This document traces the complete path of a transaction from receipt to state commitment,
identifying every shared state mutation and the dependency graph between transactions.

## Execution Flow

### Block Production Lifecycle

```
start_block()                          [controller.cpp:3217]
  → create block-level undo session    [controller.cpp:3245]
  → activate protocol features         [controller.cpp:3270-3332]
  → execute on-block transaction       [controller.cpp:3373]

push_transaction() × N                 [controller.cpp:3058]
  → create tx-level undo session       [transaction_context.cpp:73]
  → init_for_input_trx()              [transaction_context.cpp:279]
  → exec()                            [transaction_context.cpp:347]
  → finalize()                        [transaction_context.cpp:386]
  → squash() or undo()                [transaction_context.cpp:450-458]

finalize_block()                       [controller.cpp:3413+]
commit_block()                         [controller.cpp:3501]
  → push block-level session           [controller.cpp:3600]
```

### Transaction Context Initialization

`init_for_input_trx()` (transaction_context.cpp:279-328):

1. Validates transaction structure
2. Checks expiration and TaPoS (Transaction as Proof of Stake)
3. Calculates network bandwidth cost
4. Calls `init(initial_net_usage)` which:
   - Reads `global_property_object.configuration`
   - Reads block NET/CPU limits from `resource_limits_manager`
   - Updates account usage: `rl.update_account_usage(bill_to_accounts, slot)` [line 185]
   - Populates `bill_to_accounts` and `validate_ram_usage` sets
5. Records transaction ID: `record_transaction(id, trx.expiration)` [line 326]

### Action Execution

`exec()` (transaction_context.cpp:347-384):

```
for each context_free_action:
    schedule_action(action, account, true)

for each regular_action:
    schedule_action(action, account, false)

for i in 1..num_original_actions:
    execute_action(i, 0)
    // This may spawn inline actions via require_recipient()
    // Those get appended and executed in order
```

`execute_action()` → `apply_context::exec_one()` (apply_context.cpp:63-193):

1. Look up receiver account: `db.get<account_metadata_object, by_name>(receiver)` [line 83]
2. Try native handler: `control.find_apply_handler(receiver, act->account, act->name)` [line 86]
3. If no native handler, execute WASM: `control.get_wasm_interface().apply(code_hash, ...)` [line 107]
4. Generate action receipt with sequence numbers [lines 159-179]

### Finalization

`finalize()` (transaction_context.cpp:386-448):

1. Update permission usage timestamps [line 401]
2. Verify RAM usage per account [lines 406-409]
3. Recalculate NET/CPU limits [lines 411-432]
4. Bill resources: `rl.add_transaction_usage(bill_to_accounts, cpu, net, slot)` [line 446]

## All Shared State Mutations

### Consensus State (Strictly Order-Dependent)

| Object | Field | Mutated By | Location |
|--------|-------|-----------|----------|
| `dynamic_global_property_object` | `global_action_sequence` | Every action | apply_context.cpp:1043 |
| `account_metadata_object` | `recv_sequence` | Per receiver | apply_context.cpp:1055 |
| `account_metadata_object` | `auth_sequence` | Per authorizer | apply_context.cpp:1063 |

These three counters are the tightest coupling. Every action increments `global_action_sequence`,
and the resulting value is baked into the action receipt, which feeds into the block's Merkle root.

```cpp
// apply_context.cpp:1037-1067
uint64_t apply_context::next_global_sequence() {
    const auto& p = control.get_dynamic_global_properties();
    db.modify(p, [&](auto& dgp) { ++dgp.global_action_sequence; });
    return p.global_action_sequence;
}

uint64_t apply_context::next_recv_sequence(const account_metadata_object& ra) {
    db.modify(ra, [&](auto& a) { ++a.recv_sequence; });
    return ra.recv_sequence;
}

uint64_t apply_context::next_auth_sequence(account_name actor) {
    const auto& amo = db.get<account_metadata_object, by_name>(actor);
    db.modify(amo, [&](auto& am) { ++am.auth_sequence; });
    return amo.auth_sequence;
}
```

### System State

| Object | Field | Mutated By | Location |
|--------|-------|-----------|----------|
| `account_metadata_object` | `code_hash`, `code_sequence`, `abi_sequence` | setcode/setabi actions | Native handlers |
| `permission_object` | `last_used` | Per authorization | transaction_context.cpp:401 |
| `resource_usage_object` | CPU/NET deltas | Per billed account | transaction_context.cpp:185, 446 |
| `protocol_state_object` | `activated_protocol_features` | start_block only | controller.cpp:3330 |
| `global_property_object` | `proposed_schedule*` | start_block only | controller.cpp:3357 |

### Contract State

| Object | Operation | Keyed By | Location |
|--------|-----------|----------|----------|
| `key_value_object` | create/modify/remove | (table_id, primary_key) | apply_context.cpp:788-921 |
| `table_id_object` | create/modify count/remove | (code, scope, table) | apply_context.cpp:666-716 |
| `index64_object` | create/modify/remove | (table_id, secondary, primary) | database.cpp |
| `index128_object` | create/modify/remove | (table_id, secondary, primary) | database.cpp |
| `index256_object` | create/modify/remove | (table_id, secondary, primary) | database.cpp |
| `index_double_object` | create/modify/remove | (table_id, secondary, primary) | database.cpp |
| `index_long_double_object` | create/modify/remove | (table_id, secondary, primary) | database.cpp |
| `generated_transaction_object` | create/remove | (sender, sender_id) | transaction_context.cpp |

## Action Receipt Structure

```cpp
struct action_receipt {
    account_name receiver;                              // static
    digest_type act_digest;                             // content hash (order-independent)
    uint64_t global_sequence;                           // ORDER-DEPENDENT
    uint64_t recv_sequence;                             // ORDER-DEPENDENT (per receiver)
    flat_map<account_name, uint64_t> auth_sequence;     // ORDER-DEPENDENT (per authorizer)
    fc::unsigned_int code_sequence;                     // from account metadata
    fc::unsigned_int abi_sequence;                      // from account metadata
};
```

The receipt feeds into the block's action Merkle root. For deterministic consensus,
receipts must be identical across all validating nodes. This means the execution order
must produce identical sequence numbers.

## Dependency Graph Between Transactions

### Hard Dependencies (Same Account)

Two transactions conflict if they both write to the same account's:
- `recv_sequence` (both send actions to same receiver)
- `auth_sequence` (both authorized by same actor)
- `resource_usage_object` (both bill same account)

### Contract State Dependencies

Two transactions conflict if they both access the same:
- `(code, scope, table, primary_key)` — row-level conflict
- `(code, scope, table)` — table creation/deletion conflict

### The Global Sequence Problem

`global_action_sequence` is incremented by EVERY action. This creates a total ordering
dependency across all transactions. This is the single hardest problem for parallelization.

**Solutions:**
1. **Defer assignment** — execute in parallel, assign sequences in a deterministic post-pass
   based on transaction index in the block
2. **Remove from receipt** — protocol change to remove global_sequence from the receipt digest
3. **Per-shard sequences** — if sharding, each shard has its own sequence space

Option 1 is the most practical for Block-STM: execute speculatively without assigning
global_sequence, then assign in original transaction order after all executions complete.

## Read/Write Set Summary for Block-STM

For each transaction, the read/write set consists of:

**Reads:**
- Account metadata (receiver lookup, code_hash, vm_type)
- Permission objects (authorization checks)
- Resource limits (CPU/NET availability)
- Contract tables (db_find_i64, db_get_i64, secondary index lookups)
- Global properties (chain configuration)

**Writes:**
- Sequence counters (global, recv, auth)
- Resource usage (CPU/NET billing)
- Permission usage timestamps
- Contract tables (db_store/update/remove)
- Table metadata (row count)
- RAM accounting

**Conflict Detection Granularity:**

| State Type | Optimal Granularity | Conflict Rate |
|-----------|---------------------|---------------|
| Contract tables | (code, scope, table, primary_key) | Low — most txns touch different contracts |
| Sequence counters | Per-account for recv/auth | Medium — popular accounts (exchanges, DEXes) |
| Resource limits | Per-account | Low — accounts rarely share billing |
| Global sequence | Global singleton | 100% — every txn conflicts |

The key insight: if global_sequence assignment is deferred, the remaining conflicts are
**per-account** and **per-table-row**. In practice, most transactions in a block touch
different accounts and different contract tables, making Block-STM highly effective.
