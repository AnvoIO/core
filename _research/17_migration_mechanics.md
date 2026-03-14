# Migration Mechanics — EOSIO Chain Upgrade Path

## The Promise

Existing EOSIO chains can migrate to this fork:
1. Take state snapshot on current chain
2. Import snapshot into new chain's genesis
3. All accounts, balances, contracts, table data preserved
4. Chain boots with new consensus and features
5. Existing contracts execute without recompilation

## Snapshot Format Compatibility

### Spring Snapshot Format

Spring uses a binary snapshot format containing:
- Database header (version, architecture, features)
- All ChainBase index data (accounts, permissions, contract tables, resource limits)
- Block state (head block, LIB, fork database)
- Protocol feature activation state

### Architecture Tag

`libraries/chaindb/include/chainbase/environment.hpp` stores architecture in the
database header:

```cpp
enum arch_t : unsigned char {
    ARCH_X86_64,
    ARCH_ARM,
    ARCH_RISCV,
    ARCH_OTHER
};
```

**Snapshots are architecture-specific.** An x86_64 snapshot can only be loaded
on x86_64. An AArch64 snapshot requires AArch64. This is correct behavior —
ChainBase uses memory-mapped files with architecture-dependent byte ordering
and pointer sizes.

**Cross-architecture migration:** Export snapshot on old chain (x86_64) → import
on new chain (x86_64 or AArch64). The snapshot import process reconstructs state
from a portable format, not from the raw memory map. So cross-architecture import
should work — verify during testing.

## Compatible Source Chains

| Source Chain Version | Snapshot Compatible | Notes |
|---|---|---|
| Spring 1.x (current Antelope) | YES | Direct snapshot import |
| Leap 3.x-5.x | YES | Same snapshot format as Spring |
| Leap 2.x | LIKELY | May need format conversion |
| EOSIO 2.1 | MAYBE | Older format, needs testing |
| EOSIO 2.0 and earlier | NO | Must upgrade to Leap first |

**Practical path for old chains:** Upgrade to Leap 5.x or Spring first (well-documented
process), then snapshot and import into the fork.

## Migration Scenarios

### Scenario A: Chain on Spring/Leap 3.x+ (Easy)

```
1. Chain takes portable snapshot: snapshot-util --export-snapshot snapshot.bin
2. Fork node imports: cored --snapshot snapshot.bin --genesis genesis.json
3. Deploy updated system contract
4. Activate new protocol features (gas mode, etc.)
5. Chain is live on the fork
```

**Complications:**
- System contract must be replaced (old eosio.system → new system contract)
- Protocol features need activation in correct order
- Genesis must include the fork's required initial state

### Scenario B: Chain with Custom System Contract (Medium)

Many EOSIO chains have modified their system contract (Telos, WAX, Libre all have
custom versions with different tokenomics, resource models, voting rules).

```
1. Import snapshot (preserves all state including custom system tables)
2. Deploy fork's system contract (replaces custom contract)
3. Run migration action:
   - Map old system tables to new schema
   - Convert old resource model state to new model
   - Preserve token balances, staking, voting state
   - Set up gas pricing parameters
   - Configure deposit parameters
4. Activate new protocol features
```

The migration action is chain-specific — each migrating chain needs a tailored
migration contract that understands their custom system tables.

### Scenario C: Chain with Different Token Symbol (Easy)

The system contract's `init(version, core_symbol)` action accepts the core token
symbol as a parameter. Import snapshot, deploy system contract, call init with the
chain's existing token symbol.

No state changes needed — token balances are stored in eosio.token tables which
are symbol-aware.

### Scenario D: RAM Market Migration (Medium)

The fork removes the Bancor RAM market. Migrating chains have RAM market state
(connector balances, supply quotas, per-account RAM purchases).

**Migration approach:**
```
1. Snapshot preserves all RAM market tables
2. Migration action:
   a. Read each account's RAM allocation from old system tables
   b. Convert to fixed-allocation model:
      - Each account keeps their current RAM bytes
      - Bancor market tables are archived (not deleted, for audit)
      - New RAM pricing parameters set by governance
   c. Accounts that were "holding" RAM as speculation:
      - Their RAM allocation remains (they bought it)
      - They can't sell it back at Bancor prices (market is gone)
      - They can release unused RAM at fixed governance price
3. Net effect: everyone keeps what they have, market mechanics change going forward
```

## Migration Contract Template

A reusable migration contract for each scenario:

```cpp
class migration : public contract {
public:
    // Phase 1: Validate pre-migration state
    ACTION validate() {
        // Check system contract version
        // Check protocol features
        // Check token symbol
        // Report readiness
    }

    // Phase 2: Transform system state
    ACTION migrate(uint64_t batch_start, uint64_t batch_size) {
        // Process accounts in batches (avoid timeout)
        // Convert resource model state
        // Set baseline allocations
        // Archive deprecated tables
    }

    // Phase 3: Configure new features
    ACTION configure() {
        // Set gas prices
        // Set deposit parameters
        // Set baseline allocation parameters
        // Activate governance parameters
    }

    // Phase 4: Verify post-migration state
    ACTION verify() {
        // Check all accounts have correct resource allocations
        // Verify token supply matches
        // Confirm no data loss
    }
};
```

## Migration Tools

| Tool | Purpose | Effort |
|---|---|---|
| **Snapshot validator** | Verify snapshot compatibility before import | 1-2 weeks |
| **Migration contract** | Transform legacy state to new model | 2-3 weeks |
| **Migration verifier** | Compare pre/post migration state | 1 week |
| **Migration guide** | Step-by-step documentation per source chain type | 1-2 weeks per chain |
| **Test harness** | Automated migration testing with real chain snapshots | 2-3 weeks |
| **Total** | | **~2-3 months** |

## Testing Strategy

### Unit Testing
- Import known-good snapshots from EOS, Telos, WAX, Libre testnets
- Run migration contract
- Verify all accounts, balances, permissions preserved
- Verify contract tables intact
- Deploy test contracts, verify execution matches source chain

### Integration Testing
- Full chain boot from migrated snapshot
- Run existing chain's test suite against migrated state
- Verify block production, consensus, finality all work
- Test new features (gas, passkeys) on migrated state

### Regression Testing
- Any contract that worked on the source chain must work on the fork
- Token transfers, staking, voting, multisig — all must match
- Resource limits must be equivalent or better (never worse)

## Key Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Snapshot format version mismatch | Import fails | Snapshot validator catches this pre-import |
| Custom system contract table incompatibility | Migration contract fails | Per-chain migration contract with schema mapping |
| Protocol feature ordering | Chain fails to boot | Document exact activation sequence |
| RAM market state conversion | Accounts lose RAM value | Conservative conversion: keep existing allocations |
| Token supply mismatch after migration | Economic integrity broken | Migration verifier checks total supply before/after |
| Contract behavior difference | dApps break | Run source chain's test suite post-migration |

## The Pitch to Existing Chains

```
"Your data is safe. Every account, every balance, every deployed
contract comes with you. Your users don't re-register. Your developers
don't redeploy. You get parallel execution, ARM support, passkey accounts,
gas payments, and BTC/ETH bridges. Same chain, better engine."
```
