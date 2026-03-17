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

## Protocol Feature Activation

The resource model changes require protocol feature activation, similar to how
Savanna consensus is activated. The `gas_payment_mode` feature must be:

1. Defined in the node binary (feature digest, description, dependencies)
2. Pre-activated by a producer via `preactivate_feature`
3. Voted on by 2/3+ of active producers
4. Activated in a subsequent block after sufficient agreement

Chains that don't activate the feature run the existing staking-only model
unchanged. This is the same mechanism used for `WEBAUTHN_KEY`, `BLS_PRIMITIVES2`,
and all other Antelope protocol upgrades.

## Libre Chain Reference: Account Resource Allocator

Libre Chain faced the same baseline allocation problem and solved it with an
external TypeScript service (`account-resource-allocator`) that:

- Polls accounts registered in a vault table
- Monitors CPU/NET/RAM usage against configurable minimum thresholds
  (`MIN_CPU_LIMIT`, `MIN_NET_LIMIT`, `MIN_RAM_LIMIT`)
- Calls `setalimits` to replenish resources when accounts drop below thresholds
- Runs as a daemon outside the chain

**Assessment:** This is a band-aid approach — an off-chain service doing what
should be a protocol-level guarantee. It has single-point-of-failure risk (if the
service stops, accounts run out of resources), requires infrastructure to operate,
and doesn't scale to millions of accounts.

Anvo's design supersedes this entirely by making baseline allocation a protocol-level
feature: every account gets a free baseline at creation time, enforced by the node,
with no external service needed. The Libre approach validates the need but not the
solution.

## Antelope Chain Findings: RAM & Resource Models

### Fixed-Price RAM (UX Network — Production Validated)

UX Network replaced the Bancor RAM market with fixed-price RAM: `1 UTXRAM = 1 KB`.
RAM fees are disabled entirely. This is the same direction Anvo plans (governance-set
pricing, no speculation). UX validates that this works in production without issues.

Their implementation completely bypasses the Bancor algorithm — the `rammarket` table
still exists for compatibility but price calculations are replaced with fixed math.
RAM must be bought/sold in exact KB increments.

**Discussion:** Should Anvo keep sub-KB granularity (byte-level pricing) or adopt
UX's KB-increment approach? Byte-level is more flexible but KB-level is simpler.
The governance-set price per byte approach in doc 11 is more flexible than UX's
fixed 1:1 ratio and is recommended.

### Fee-Offset Inflation (WAX — Key Idea for Gas Model)

WAX collects fees from Powerup and uses them to offset inflation in real-time:

```
distribute_tokens = continuous_rate × token_supply × time_fraction
fees_collected = balance_of(eosio.fees)
fees_to_use = min(distribute_tokens, fees_collected)
tokens_to_issue = distribute_tokens - fees_to_use
// Excess fees beyond what's needed are BURNED via token::retire
```

This creates a path to zero or negative net inflation during high chain activity.

**Discussion:** This maps directly to Anvo's gas model. Gas fees collected could
offset the inflation budget before any new tokens are issued. During high utilization,
gas revenue could exceed the inflation target, making the token deflationary. This
should be designed into the fee distribution mechanism (doc 11, "Fee Distribution"
section). The burn-excess-fees approach is simple and effective — no complex
tokenomics needed, just `if fees > budget: burn(fees - budget)`.

### REX Removal (WAX, Libre)

Both WAX and Libre removed REX entirely. With alternative resource models (Powerup
on WAX, external allocator on Libre), REX added complexity without proportional
benefit.

**Discussion:** Anvo's gas model likely makes REX unnecessary. Users who want
sustained throughput stake tokens (existing `delegatebw`). Users who want
occasional throughput pay gas. The REX lending market was designed to solve the
problem of unused staked resources — gas solves this more cleanly. Recommend
removing REX from the system contracts unless a specific use case emerges.

### Usage-Proportional Inflation (UX Network — Study Further)

UX Network's most novel idea: inflation tokens are distributed to users proportional
to their actual CPU usage, not to stakers. BPs serve as oracles reporting per-account
usage data with a modal-hash consensus mechanism.

The inflation formula uses an entropy-inspired function: `C(x) = -x × ln(x) × e`,
where x is utilization. Inflation is inversely proportional to utilization — low
usage triggers higher inflation to incentivize growth; high usage reduces inflation.
A decaying value-transfer rate with a 365-day half-life limits total issuance.

**Discussion:** This is intellectually interesting but adds significant complexity
(oracle reporting, consensus on usage data, per-account tracking). For Anvo, the
gas model already creates usage incentives (fees fund the network). However, the
concept of rewarding active users — not just stakers — is worth revisiting for
Phase 3 tokenomics. A simpler version might rebate a percentage of gas fees to
active accounts, funded from the inflation budget.

### Fee Distribution Router (Vaulta — Key Pattern)

Vaulta's `eosio.fees` contract routes all collected fees through a single contract
with configurable distribution strategies:

- **Strategies table** with configurable weights: `buyramburn`, `buyramself`,
  `donatetorex`, `eosio.bpay`, `eosio.bonds`
- **Epoch system** — configurable time interval (default 10 minutes). `distribute()`
  callable by anyone but only after epoch completes.
- All fee sources (RAM fees, name bid fees, gas fees) channel to `eosio.fees`,
  then get distributed proportionally to strategies.

**Discussion:** This is a cleaner architecture than WAX's inline fee-offset approach.
A single fee router with governance-adjustable weights means the fee distribution
can be rebalanced (e.g., 40% burn, 30% stakers, 20% treasury, 10% BPs) without
code changes. For Anvo, gas fees should flow through a similar router contract.
The epoch-based batching reduces per-transaction overhead — fees accumulate, then
get distributed in bulk.

### Inflation Schedule System (Vaulta — Pre-Committed Tokenomics)

Vaulta added a `schedules` table storing `(start_time, continuous_rate)` pairs:

- `setschedule(start_time, continuous_rate)` — privileged action to pre-commit
  future inflation rate changes
- `execschedule()` — callable by anyone; applies the rate once the time arrives
- Also auto-executes during `claimrewards()` for guaranteed application

**Discussion:** This enables governance to pre-commit to inflation changes (e.g.,
annual halving, gradual reduction to zero) without needing future MSIGs or code
upgrades. Simple to implement and eliminates the need for manual intervention.
Strongly recommended for Anvo — define the inflation trajectory at launch and let
it auto-execute.

### Max Supply Awareness (Vaulta — Graceful Deflation Transition)

Vaulta's `claimrewards()` checks if `token_supply + new_tokens > token_max_supply`.
If so, it falls back to using existing reserves instead of minting. This enables
a clean transition from inflationary to fixed-supply economics.

**Discussion:** Combined with the inflation schedule system, this creates a smooth
path: inflation rate decreases over time via the schedule, and once supply approaches
max_supply, the system automatically stops minting and uses accumulated reserves.
Worth adopting.

### RAM Gift System (Vaulta — dApp Onboarding)

Vaulta's `giftram` action transfers encumbered RAM that can only be returned to
the gifter, not sold or traded:

- `giftram(from, to, bytes, memo)` — gift RAM to an account
- `ungiftram(from, to, memo)` — return all gifted RAM to gifter
- `gifted_ram` table tracks giftee→gifter→bytes
- `reduce_ram()` excludes gifted RAM from sellable/transferable amount

**Discussion:** Perfect for dApp-sponsored account onboarding in the gas model.
A dApp gifts RAM to new user accounts for contract interaction, but the user can't
extract value by selling the RAM. When the dApp relationship ends, the dApp reclaims
the RAM. This pairs well with the refundable deposit system — the dApp sponsors
both the deposit and the RAM, reclaims both when the user matures or leaves.
