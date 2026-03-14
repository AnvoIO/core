# Resource Model Evolution: Baseline + Staking + Gas

## Current EOSIO Model

Three resources, each with friction:

**CPU & NET (Bandwidth):**
- Stake tokens → get proportional share of block bandwidth
- Elastic: expands up to 1000x when idle, contracts when congested
- 24-hour EMA tracks usage — burst OK, sustained hits limits
- Exceed allocation → transaction rejected

**RAM (Storage):**
- Buy on Bancor market (price-discovery via constant reserve ratio)
- Consumed when storing on-chain data
- Sellable, but market is speculative — prices can spike

**The Problem:**
1. Users need resources before their first transaction (onboarding cliff)
2. CPU allocation is unpredictable (depends on global congestion)
3. RAM is a speculative asset (hoarding drives up prices)
4. Every dApp must solve "who pays?" differently
5. Developers from other ecosystems bounce off the complexity

**What's Great About It (Don't Lose This):**
- Free transactions for users when dApp stakes resources
- No gas price wars, no MEV, no stuck transactions
- Predictable costs for operators (stake once, get throughput)
- Elastic scaling (1000x expansion when chain is idle)

## New Model: Baseline + Staking + Parallel Gas

### Concept

Two parallel paths to the same resources. Per-transaction, the user or dApp
chooses which one:

**Path 1: Staking (Free Transactions)**
- Account has staked allocation (baseline + staked boost)
- Transaction consumes bandwidth from allocation
- No fee charged
- Works exactly like today's EOSIO model

**Path 2: Gas (Pay Per Transaction)**
- Transaction includes a gas payment flag/field
- Resources consumed are billed at a known price
- Fee deducted from payer's token balance
- No staking needed — just hold tokens

These are NOT layered (gas doesn't kick in after staking is exhausted). They're
**parallel paths** — each transaction uses one or the other.

### Three Tiers of Resource Access

**Tier 1: Free Baseline**
Every account gets a baseline CPU/NET/RAM allocation just for existing.
Enough for normal user activity (few transactions per day). No staking,
no tokens needed for basic use.

**Tier 2: Staking Boost**
Stake tokens to increase allocation above baseline. Same elastic model
as today — proportional share, 24-hour EMA, congestion-responsive.
For power users, dApps, and infrastructure.

**Tier 3: Gas Payment**
Pay per-transaction at a governance-set price. No staking required,
no allocation needed. Just hold tokens and transact. For new users,
occasional users, developers coming from other ecosystems.

### User Experience by Profile

| User Type | Experience |
|---|---|
| Casual user (few tx/day) | Free. Baseline covers everything. Never thinks about resources. |
| New user from Ethereum | Send tokens, pay gas, transact immediately. Familiar model. |
| Active dApp user | dApp stakes on their behalf → free. Or user pays gas. Their choice. |
| dApp developer (new) | Deploy contract, it just works. Users pay gas or dApp stakes. No tutorial. |
| dApp developer (experienced) | Stakes for users, free-tx UX. Competitive advantage over gas chains. |
| Power user / whale | Stakes heavily, never pays gas. Same experience as today. |
| Bot / high-frequency | Stakes for sustained throughput. Gas would be expensive at volume. |

### RAM in This Model

Same parallel-path philosophy:
- **Free baseline** per account (governance-set, e.g., 8-64KB)
- **Buy more** at governance-set pricing (NOT Bancor speculation)
- **Or pay gas** — RAM cost included in gas price for transactions that store data

The Bancor RAM market is removed. No more speculation. Storage costs are
predictable and governance-controlled.

## Transaction Flow

```
Transaction arrives
    |
    ├─ gas_payer field set?
    |       |
    |       YES → Gas Path
    |       |   → Execute transaction
    |       |   → Measure actual CPU (μs) + NET (bytes) + RAM delta (bytes)
    |       |   → Calculate fee:
    |       |       cpu_cost = cpu_us × gas_price_per_cpu_us
    |       |       net_cost = net_bytes × gas_price_per_net_byte
    |       |       ram_cost = ram_delta × gas_price_per_ram_byte
    |       |       total = cpu_cost + net_cost + ram_cost
    |       |   → Deduct total from gas_payer's token balance
    |       |   → Resources NOT charged against any staked allocation
    |       |   → Block-level limits still enforced (DoS prevention)
    |       |   → Commit
    |       |
    |       NO → Staking Path
    |           → Check account's allocation (baseline + staked, elastic)
    |           → Execute transaction
    |           → Check: within allocation?
    |               → YES → Commit (free)
    |               → NO → Reject (tx_cpu_usage_exceeded)
    |
```

### Gas Payer Options

The `gas_payer` field enables flexible payment patterns:

| Pattern | gas_payer | Use Case |
|---|---|---|
| Self-pay | Transaction sender | Default, like Ethereum |
| dApp sponsors | Contract account | dApp pays gas for its users |
| Third-party sponsor | Any account that co-signs | Onboarding service pays for new users |

The gas_payer must authorize the transaction (co-sign) to prevent unauthorized billing.

## Implementation

### Node-Level Changes

The resource model lives in two layers:

**Layer 1: Node (consensus-critical)**
- `resource_limits_manager` — already tracks weights, usage, elastic limits
- `transaction_context` — already handles billing in `finalize()`
- Changes needed: add gas path branching in transaction processing

**Layer 2: System contract (governance)**
- Sets baseline allocations, gas prices, staking rules
- Calls privileged intrinsics (`set_resource_limits`, `set_resource_parameters`)
- Changes needed: new gas pricing actions, baseline allocation logic

### Node Changes (Contained)

**transaction_context.cpp — init():**
```cpp
// Existing: calculate resource limits from staking allocation
// New: if gas_payer set, skip allocation checks (use block limits only)
if (trx.gas_payer.has_value()) {
    gas_mode = true;
    // Block-level limits still apply (DoS prevention)
    net_limit = block_net_available;
    cpu_limit = block_cpu_available;
} else {
    // Existing staking path unchanged
    net_limit = min(block_available, account_available, ...);
    cpu_limit = min(block_available, account_available, ...);
}
```

**transaction_context.cpp — finalize():**
```cpp
if (gas_mode) {
    // Calculate gas cost from actual usage
    int64_t gas_cost = calculate_gas_fee(billed_cpu_us, net_usage, ram_delta);

    // Deduct from gas_payer via native action or inline transfer
    deduct_gas_fee(gas_payer, gas_cost);

    // Do NOT charge against gas_payer's staked allocation
} else {
    // Existing: charge against staked allocation, reject if exceeded
    rl.add_transaction_usage(bill_to_accounts, billed_cpu_us, net_usage, ...);
}
```

**Transaction format:**
```cpp
struct transaction {
    // ... existing fields ...
    std::optional<account_name> gas_payer;  // NEW: if set, use gas path
};
```

Adding an optional field to the transaction format is a **protocol-level change**
requiring a consensus upgrade (protocol feature activation). But it's backward
compatible — existing transactions without the field use the staking path.

### System Contract Changes

| Component | Change |
|---|---|
| Gas pricing | New table: `gas_params` (cpu_price, net_price, ram_price) |
| Gas price governance | New action: `setgasprices(cpu, net, ram)` (requires producer vote) |
| Baseline allocation | On account creation: `set_resource_limits(account, baseline_ram, baseline_cpu_weight, baseline_net_weight)` |
| Fee collection | Gas fees collected to system account or burned (governance decision) |
| RAM market removal | Remove Bancor actions, replace with fixed-price allocation |
| Staking (unchanged) | `delegatebw` / `undelegatebw` work exactly as before |

### Protocol Feature

Activated via protocol feature (like all consensus changes in Antelope):

```cpp
builtin_protocol_feature_t::gas_payment_mode
```

When activated:
- Transaction format accepts optional `gas_payer` field
- Node processes gas-flagged transactions via gas path
- System contract must have gas pricing configured
- Existing transactions (no gas_payer) work exactly as before

When NOT activated:
- Transactions with `gas_payer` are rejected
- 100% backward compatible with existing behavior

## Impact on Legacy EOSIO Chains

### Migrating Chains (Activating the Protocol Feature)

**Zero breaking changes:**
- All existing transactions continue to work (staking path, no gas_payer field)
- All existing contracts continue to work (no ABI change for contracts)
- All existing staking (delegatebw, undelegatebw) continues to work
- All existing resource limits continue to work

**What changes when activated:**
- New transaction type available (with gas_payer)
- System contract needs updating to support gas pricing
- New users can transact by paying gas instead of staking
- Existing users can choose gas OR staking per-transaction

**Migration path:**
1. Upgrade node software (adds protocol feature support)
2. Deploy updated system contract (adds gas pricing, baseline allocation)
3. Producers vote to activate `gas_payment_mode` protocol feature
4. Gas pricing takes effect — both paths available immediately
5. No existing transactions, contracts, or accounts are affected

### Chains That DON'T Activate

Nothing changes. The protocol feature is opt-in. Chains that prefer the
pure staking model keep it exactly as-is. The gas path code exists in the
node binary but is dormant until the feature is activated.

### SDK / Tooling Impact

- **Existing SDKs** continue to work (transactions without gas_payer)
- **Updated SDKs** can add gas_payer field to transaction construction
- **Wallets** can offer "pay gas" as an alternative to "stake resources"
- **Block explorers** show gas fees alongside resource usage

### Contract Compatibility

**Existing contracts: zero changes required.**

Contracts don't know or care how resources were paid for. The billing happens
in the transaction context layer, outside the contract's execution. A contract
that works today works identically whether the transaction used staking or gas.

The only new contract-level capability: a contract can act as a gas sponsor
by being set as `gas_payer` on transactions that interact with it.

## Fee Distribution

Gas fees collected can be distributed via governance:

| Option | How It Works | Precedent |
|---|---|---|
| **Burn** | Fees destroyed, deflationary | Ethereum EIP-1559 |
| **Validators** | Fees distributed to block producers/finalizers | Solana, Cosmos |
| **Treasury** | Fees go to community fund | Polkadot |
| **REX/Stakers** | Fees go to token stakers | Current EOSIO RAM fees |
| **Hybrid** | Burn X%, validators Y%, treasury Z% | Ethereum (base + priority) |

This is a governance decision, configurable in the system contract. Not hardcoded
in the node.

## Gas Price Setting

### Static (Governance-Set)

Producers vote on gas prices (e.g., quarterly):
```
cpu_price: 0.0001 TOKEN per CPU microsecond
net_price: 0.00001 TOKEN per NET byte
ram_price: 0.001 TOKEN per RAM byte
```

Simple, predictable, easy to understand. Downside: doesn't respond to congestion.

### Dynamic (EIP-1559 Style)

Base price adjusts automatically based on block utilization:
```
if block_utilization > target:
    base_price *= 1.125  (increase 12.5%)
else:
    base_price *= 0.875  (decrease 12.5%)
```

Responds to congestion, prevents spam during high demand. More complex.

**Recommendation for launch:** Static governance-set prices. Add dynamic pricing
later if needed. Keep it simple.

## Comparison With Other Chains

| Feature | Anvo | Ethereum | Solana | Aptos | Sui |
|---|---|---|---|---|---|
| Free transactions possible | YES (staking path) | No | No | No | No |
| Gas available | YES (gas path) | Yes (only option) | Yes (only option) | Yes (only option) | Yes (only option) |
| User chooses payment method | YES | No | No | No | No |
| dApp can sponsor users | YES (both paths) | Via relayers only | No | Sponsor API | Sponsor API |
| Baseline free allocation | YES | No | No | No | No |
| Predictable costs via staking | YES | No | No | No | No |
| No RAM speculation | YES (fixed price) | N/A (no RAM concept) | N/A | Storage deposits | Storage deposits |

**No other chain offers the choice.** This is a genuine differentiator.

## Account Creation: Refundable Deposit

### The Spam Problem

Permissionless account creation + gas payment = anyone can create millions of
accounts that bloat chain state permanently. Accounts can't be garbage collected —
they may be referenced by contracts, permissions, or pending transactions.

Every account costs the network:
- ~300 bytes for account object
- ~200+ bytes per permission structure
- Free baseline CPU/NET/RAM allocation
- Index entries in every node's state database forever

### Solution: Refundable Deposit with Maturity Conditions

Account creation requires a **token deposit** (not a fee). The deposit is locked,
not burned. It's returned to the account holder once the account proves it's a
real user by meeting maturity conditions.

```
Create account:
  → lock DEPOSIT_AMOUNT (e.g., 1.0 TOKEN)
  → account is active immediately
  → deposit tracked in system contract table

Maturity conditions (ALL must be met):
  → account age >= MIN_AGE (e.g., 30 days)
  → transaction count >= MIN_TRANSACTIONS (e.g., 10)
  → (optional) account has non-zero token balance beyond deposit

Claim refund:
  → user calls reclaimdeposit action
  → system contract checks maturity conditions
  → conditions met → deposit returned to account
  → conditions not met → rejected, try again later
```

### Why This Works

**Spammer economics:**
```
Create 1 million accounts = lock 1,000,000 TOKEN
Wait 30 days + make 10 transactions each = operational overhead
Even if they eventually reclaim, capital is locked for a month minimum
Opportunity cost + operational cost makes spam uneconomical
```

**Legitimate user experience:**
```
dApp sponsors deposit on account creation (gas_payer covers it)
User uses the dApp normally over a few weeks
After 30 days + 10 transactions: deposit auto-refunded (or dApp reclaims)
User never knew there was a deposit
```

### Configuration (Governance-Set)

| Parameter | Default | Description |
|---|---|---|
| `deposit_amount` | 1.0 TOKEN | Required deposit for account creation |
| `min_account_age` | 30 days | Minimum age before refund eligible |
| `min_transactions` | 10 | Minimum transaction count before refund eligible |
| `deposit_sponsor_allowed` | true | Whether a third party can pay the deposit |

All parameters adjustable by governance (producer vote). Chain can tighten or
loosen anti-spam measures based on observed behavior.

### Who Pays the Deposit

| Pattern | Who Locks Tokens | Who Reclaims |
|---|---|---|
| Self-funded user | User | User (after maturity) |
| dApp-sponsored | dApp account | dApp account (refund goes back to sponsor) |
| Faucet/onboarding service | Service account | Service account (recycles deposits) |

When a dApp sponsors account creation, the deposit comes from the dApp's balance.
When the account matures, the deposit returns to the dApp — not the user. This
lets dApps run a self-sustaining onboarding loop: deposit → create account →
user matures → deposit returned → create next account.

### Interaction with Gas Model

The deposit is separate from gas:

```
Account creation transaction:
  gas cost:  0.01 TOKEN (covers CPU/NET/RAM for the newaccount action)
  deposit:   1.00 TOKEN (locked, refundable)
  total:     1.01 TOKEN (deducted from creator/sponsor)
  refundable: 1.00 TOKEN (after maturity conditions met)
  net cost:  0.01 TOKEN (just the gas)
```

### Account Deletion (Optional, Future)

For accounts that never mature (abandoned spam):

```
If account age > ABANDONMENT_PERIOD (e.g., 1 year) AND:
  - zero token balance (beyond deposit)
  - zero RAM usage (no stored data)
  - no contract deployed
  - not referenced by other accounts' permissions
Then:
  - deposit is burned (or sent to treasury)
  - account state can be pruned from active state
  - reduces ongoing storage burden
```

This is a future optimization, not required at launch. But it closes the loop:
spam accounts that never mature eventually lose their deposit AND get cleaned up.

### Implementation

**All system contract logic — no node changes:**

| Component | Where | Effort |
|---|---|---|
| Deposit table (tracks deposits, maturity) | System contract | 1 week |
| Modified `newaccount` (require deposit) | System contract | 1 week |
| `reclaimdeposit` action | System contract | 3 days |
| Governance parameter management | System contract | 3 days |
| Sponsor refund routing | System contract | 3 days |
| Testing | System contract tests | 1 week |
| **Total** | | **~3-4 weeks** |

## Implementation Effort

| Component | Where | Effort |
|---|---|---|
| Protocol feature definition | Node (C++) | 1-2 days |
| Transaction format extension | Node (C++) | 1-2 days |
| Gas path in transaction_context | Node (C++) | 1-2 weeks |
| Block-level gas accounting | Node (C++) | 1 week |
| System contract: gas pricing | System contract (C++) | 1-2 weeks |
| System contract: baseline allocation | System contract (C++) | 1 week |
| System contract: RAM model change | System contract (C++) | 1-2 weeks |
| System contract: account deposit + maturity | System contract (C++) | 3-4 weeks |
| Testing (unit + integration) | Both | 2-3 weeks |
| SDK updates | TypeScript/Rust | 1-2 weeks |
| **Total** | | **~2-3 months** |

Most of the work is in the system contract, not the node. The node changes
are contained to `transaction_context.cpp` and the transaction serialization.
