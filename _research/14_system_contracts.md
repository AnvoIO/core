# System Contracts — Starting Point & Modifications

## Overview

Spring bundles complete, tested system contracts at `unittests/contracts/`. These are
the starting point for the fork. No external dependency needed.

## Bundled Contracts

| Contract | Location | Purpose |
|---|---|---|
| **eosio.system** | `unittests/contracts/eosio.system/` | Full system contract: producers, voting, resources, REX, rewards |
| **eosio.boot** | `unittests/contracts/eosio.boot/` | Minimal bootstrap for chain initialization |
| **eosio.token** | `unittests/contracts/eosio.token/` | Standard token (create, issue, transfer, retire) |
| **eosio.msig** | `unittests/contracts/eosio.msig/` | Multisig proposals and execution |
| **eosio.wrap** | `unittests/contracts/eosio.wrap/` | Privileged action wrapper |

## Native Handlers (Node-Level, Not Contract)

These actions are handled natively by the node and bypass contract code entirely.
The system account prefix is genesis-configurable (`eosio` for migrating chains,
`core` for new chains — see doc 08):

```
<system>::newaccount    — account creation
<system>::setcode       — deploy/update contract code
<system>::setabi        — deploy/update contract ABI
<system>::updateauth    — modify account permissions
<system>::deleteauth    — remove custom permissions
<system>::linkauth      — link action to custom permission
<system>::unlinkauth    — unlink action from custom permission
<system>::canceldelay   — cancel delayed transaction
```

These are registered in `controller.cpp` via `SET_APP_HANDLER` macro using the
genesis-configurable system account name (doc 08).

## Privileged Intrinsics (System Contract Can Call)

Only accounts with `is_privileged()` status can call these host functions:

**Producer & Finalizer Management:**
- `set_proposed_producers(schedule)` — propose new producer schedule
- `set_proposed_producers_ex(format, schedule)` — extended format with flexible signing
- `set_finalizers(format, policy)` — set Savanna BFT finalizer set

**Resource Management:**
- `set_resource_limits(account, ram, net_weight, cpu_weight)` — set account quotas
- `get_resource_limits(account, ...)` — query account limits

**Chain Configuration:**
- `set_blockchain_parameters_packed(params)` — update chain parameters
- `set_parameters_packed(params)` — flexible parameter setter
- `set_wasm_parameters_packed(params)` — WASM VM configuration

**Feature Management:**
- `preactivate_feature(digest)` — pre-activate protocol feature
- `is_feature_active(name)` — check feature status

**Privilege Management:**
- `set_privileged(account, is_priv)` — grant/revoke privilege
- `is_privileged(account)` — check privilege status

## Onblock Action

The node automatically calls the system contract's `onblock` action every block:

```cpp
on_block_act.account = config::system_account_name;
on_block_act.name = "onblock"_n;
on_block_act.authorization = {{config::system_account_name, config::active_name}};
on_block_act.data = fc::raw::pack(head.header());
```

The system contract's `onblock` handler:
- Tracks producer unpaid blocks
- Updates producer schedule (calls `set_proposed_producers()`)
- Closes name auctions
- Manages reward buckets

## Required Modifications for Fork

### From Resource Model (Doc 11)

| Change | What | In Contract |
|---|---|---|
| Gas pricing tables | `gas_params` table: cpu_price, net_price, ram_price | core.system |
| Gas price governance | `setgasprices` action (producer vote) | core.system |
| Baseline allocation | On `newaccount`: set baseline cpu/net/ram weights | core.system |
| Refundable deposit | `deposit_table`, maturity tracking, `reclaimdeposit` action | core.system |
| Deposit sponsor tracking | Who paid deposit, who gets refund | core.system |
| Gas fee collection | Collect fees, distribute (burn/treasury/validators) | core.system |
| RAM market removal | Remove `buyram`/`sellram` Bancor logic, replace with fixed-price | core.system |
| RAM fixed pricing | Governance-set RAM price via `setramprices` | core.system |

### From Passkey Accounts (Doc 12)

| Change | What | In Contract |
|---|---|---|
| Hash-based account names | Derive name from pubkey hash on creation | core.system |
| Name service (optional) | Alias registry: readable name → hash-based account | New contract |

### From Identity (Doc 13)

| Change | What | In Contract |
|---|---|---|
| Identity registry | Layer tracking per account | New contract |
| Social vouching | Web of trust with penalties | New contract |
| Attestation framework | Generic attestor/subject/claim/expiry | New contract |
| Hardware attestation | Passkey attestation parsing | New contract |

### From Account Switch (Doc 08)

| Change | What | In Contract |
|---|---|---|
| System account names | Genesis-configurable (eosio.* or core.*) | core.system + node |

### From Rebrand (Doc 07)

| Change | What | In Contract |
|---|---|---|
| Account names in code | All `"eosio"` references → configurable | All contracts |
| ABI type names | Update any branded type names | All contracts |

## Contract Repo Strategy

**Create a new contract repository** (separate from the node repo):

```
core-contracts/
├── contracts/
│   ├── system/           ← fork of eosio.system (gas, deposits, baseline)
│   ├── token/            ← fork of eosio.token (minimal changes)
│   ├── msig/             ← fork of eosio.msig (minimal changes)
│   ├── wrap/             ← fork of eosio.wrap (minimal changes)
│   ├── boot/             ← fork of eosio.boot (updated for new features)
│   ├── identity/         ← NEW: identity registry + social vouching
│   ├── attestation/      ← NEW: generic attestation framework
│   ├── recovery/         ← NEW: social recovery contract
│   ├── names/            ← NEW: name service (optional aliases)
│   └── vault/            ← NEW: data vault (Phase 4)
├── tests/
├── scripts/
│   ├── boot.sh           ← genesis boot sequence
│   └── deploy.sh         ← contract deployment
└── CMakeLists.txt
```

**Fork from:** Spring's `unittests/contracts/` (most current with Savanna support)

**Build with:** AntelopeIO CDT (C++ contract development toolkit)

## Boot Sequence

The genesis boot sequence for a new chain:

```
1. Start core_netd with genesis.json (creates system account + initial key)
2. Deploy boot contract to system account (core)
3. Activate required protocol features:
   - PREACTIVATE_FEATURE
   - ONLY_BILL_FIRST_AUTHORIZER
   - WEBAUTHN_KEY
   - CRYPTO_PRIMITIVES
   - BLS_PRIMITIVES2
   - GAS_PAYMENT_MODE (new)
   - ... others as needed
4. Deploy token contract to token account (core.token)
5. Create core token: create(issuer, max_supply)
6. Deploy system contract to system account (core, replaces boot)
7. Initialize system: init(version, core_symbol)
8. Set gas prices: setgasprices(cpu, net, ram)
9. Set deposit parameters: setdepositparams(amount, min_age, min_txns)
10. Create additional system accounts (msig, wrap, identity, etc.)
11. Deploy remaining contracts
12. Register initial producers/finalizers
13. Chain is live
```

## Granular Permission System

Standard EOSIO has a two-tier permission system (owner/active) with `linkauth` for
custom permissions. This is flexible but coarse — there's no built-in concept of
per-action permission states or account-level capability flags.

### Libre Chain Reference: eosio.libre

Libre implemented a granular permission contract (`eosio.libre`) with 9 distinct
permission types per account:

| Permission | Controls |
|---|---|
| `createacc` | Account creation |
| `regprod` | Block producer registration |
| `vote` | Voting |
| `stake` | Token staking |
| `transfer` | Token transfers |
| `propose` | Governance proposals |
| `setcontract` | Contract deployment |
| `blacklist` | Blacklist management |
| `setalimits` | Resource limit adjustment |

Each permission has a state: `0=none`, `1=on`, `2=pending`, `3=off`, `4=banned`.
Other contracts (referrals, governance) query this table to gate actions.

### Anvo Approach: Bake Into System Contracts

Rather than a separate permission contract, bake granular capabilities into the
core system contracts from the start:

1. **Capability flags on accounts** — stored in system contract tables, queryable
   cross-contract. Controls what an account can do beyond basic transacting.

2. **Permission states** — accounts can be in different states for different
   capabilities (active, suspended, banned). Governance actions to modify.

3. **Integration with identity layers** — identity attestation levels (doc 13)
   can gate capabilities. E.g., `regprod` requires Layer 2+ identity,
   `propose` requires Layer 1+ identity.

4. **`linkauth` for fine-grained action mapping** — already protocol-level.
   Combine with capability flags for defense-in-depth.

This should be designed alongside the identity system (doc 13) since identity
layers and permission levels are closely related.

### Contract Freeze / Immutability (UX Network)

UX Network implements an `eosio.freeze` contract that allows account owners to
permanently freeze their contract code, making it impossible to update code or ABI.

- Users call `freezeacc(account)` with owner permission
- Once frozen, `setcode`/`setabi` are rejected at the system level
- Irreversible — there is no unfreeze

**Discussion:** This is a simple trust mechanism for DeFi primitives — users can
verify a contract will never change. Should be straightforward to implement as a
system-level check in the native `setcode`/`setabi` handlers. Could also be
implemented as a system contract table that the native handlers query. Consider
adding to the core system contracts for launch.

## Referral Tracking

### Libre Chain Reference: libre-referrals

Libre tracks account→referrer relationships via a simple contract:
- `add(account, referrer)` — records who referred whom
- Gated by `createacc` permission on eosio.libre
- Used for growth analytics and potential reward distribution

### Anvo Approach: Referral + Behavior Tracking

Extend the Libre model to reward good behavior and penalize bad behavior:

1. **Referral table** — simple account→referrer mapping, recorded at account creation
2. **Referrer reputation** — referrers earn reputation when their referees:
   - Reach account maturity (deposit refund eligible)
   - Maintain active usage over time
   - Receive positive attestations (identity layers)
3. **Referrer penalties** — referrers lose reputation (or privileges) when their referees:
   - Create spam accounts that never mature
   - Get banned or blacklisted
   - Engage in detectable abuse patterns
4. **Incentive distribution** — referrers with good track records earn rewards
   (portion of gas fees, token grants, increased capability flags)

This creates a self-policing growth loop: referrers are incentivized to bring in
real users, not spam accounts. The referral graph becomes a signal for identity
Layer 2 (social vouching, doc 13).

**TBD:** Detailed mechanism design. Revisit after identity system and governance
contract designs are finalized.

## Staking & Voting Power

### Libre Chain Reference: stake.libre + libre.system

Libre decouples voting power from staked token balance:

- **Stakes table**: index, account, stake_date, stake_length (days), mint_bonus,
  libre_staked, apy, payout, payout_date, status (IN_PROGRESS/COMPLETED/CANCELED)
- **Power table**: separate table mapping account → voting_power (double) +
  last_update timestamp
- **Voting power** updated on stake/unstake events, queryable cross-contract
- **Non-compounding APY** — simpler math, easier to audit
- **Time-locked staking** with configurable lengths (30/90/365 days)
- **Governance contracts** (btc-libre-governance) query the power table at
  vote-counting time, not at proposal-creation time — prevents flash-vote attacks

### Key Observations

1. **Separate voting power tracking** is valuable — allows governance weight to
   account for factors beyond raw token balance (stake duration, account age,
   identity level, behavior score)
2. **Non-compounding APY** is simpler and more predictable than compounding
3. **Time-locked tiers** (longer lock = more voting power) align incentives
4. **Cross-contract queryability** of voting power is essential for governance

**TBD:** Anvo staking and voting design. Will be informed by resource model
(doc 11), governance contract (doc 21), and identity system (doc 13).

### Antelope Chain Findings: Voting & Staking

#### Voter Rewards (WAX)

WAX allocates 40% of inflation to voters. Voters who stake and vote for producers
earn continuous rewards proportional to their voteshare over time. Key details:

- `unpaid_voteshare_change_rate` = vote weight minus proxied weight
- Share accrues continuously: `unpaid_voteshare += change_rate × elapsed_seconds`
- Claim via `voterclaim`: `reward = voters_bucket × (my_share / total_share)`
- Minimum BP count required to earn rewards (configurable)

**Discussion:** Directly paying voters incentivizes governance participation — a
chronic problem on EOSIO chains. The continuous accrual model rewards sustained
participation, not just voting once and forgetting. For Anvo, voter rewards should
be considered as part of the inflation split. The percentage (40% on WAX) may be
too aggressive — calibrate based on Anvo's tokenomics. Tie eligibility to identity
level to prevent sybil farming of voter rewards.

#### Inverse Vote Weighting (Telos)

Telos replaces time-weighted vote decay with a sine-curve formula that incentivizes
voting for more producers:

```
percentVoted = producersVotedFor / 30
voteWeight = (sin(π × percentVoted - π/2) + 1) / 2
effectiveVote = voteWeight × staked
```

Voting for 1 producer ≈ 0% weight. Voting for 15 ≈ 50%. Voting for 30 = 100%.
This prevents single-producer cartels and encourages broad governance participation.

**Discussion:** Elegant solution to vote concentration. Standard EOSIO allows
voting for up to 30 producers with equal weight per vote, which means voting for
1 producer gives that producer 100% of your weight — incentivizing cartel formation.
The sine curve smoothly penalizes narrow voting. Consider for Anvo, possibly
combined with identity-weighted voting (higher identity level = more base weight).

#### Producer Rotation (Telos)

Telos runs 42 producers (21 active + 21 standby) with 12-hour rotation:

- Every 12 hours, one active producer rotates out, one standby rotates in
- Round-robin indices (`bp_out_index`, `sbp_in_index`) cycle through positions
- Producers sorted by geographic location for diversity

**Discussion:** This gives standby producers real block production time, testing
their infrastructure continuously rather than hoping they'll perform when needed.
The 12-hour cycle is frequent enough to keep standbys sharp but infrequent enough
to maintain schedule stability. Strongly recommended for Anvo.

#### Auto-Kick with Exponential Penalty (Telos)

Telos automatically removes underperforming block producers:

- `check_missed_blocks()` runs on every `onblock`
- Producers exceeding 15% missed blocks per rotation are kicked
- Penalty: `kick_penalty_hours = 2^times_kicked` (exponential backoff)
- First kick = 2 hours, second = 4 hours, third = 8 hours, etc.
- Cap of `schedule_size / 7` kicks per rotation (prevents mass deactivation)
- Producer stats tracked: `lifetime_produced_blocks`, `lifetime_missed_blocks`,
  `times_kicked`, `kick_penalty_hours`, `last_time_kicked`

**Discussion:** Essential for network reliability. Standard EOSIO has no automatic
mechanism to remove non-performing producers — they stay in the schedule until
voted out, which can take days. The exponential penalty is well-calibrated:
occasional issues get a short timeout, chronic problems get progressively longer
bans. The per-rotation kick cap prevents cascading failures. Strongly recommended.

#### Equal BP Pay (UX Network)

UX distributes producer pay equally among all active producers in the schedule,
regardless of vote count.

**Discussion:** Standard EOSIO pays producers proportionally to votes received,
which concentrates rewards at the top. Equal pay reduces the incentive for vote
buying and makes being a mid-ranked producer more viable. Tradeoff: reduces the
incentive difference between top and marginal producers. Consider a hybrid: equal
base pay + small vote-proportional bonus.

#### Oracle-Driven Pay (Telos)

Telos uses the Delphi Oracle (BP-reported price feeds) to adjust producer pay
based on token price:

```
pay_per_month = min(378000 × (tlos_price/10000)^(-0.516), 882000)
```

As price rises, per-producer TLOS pay drops (they're already earning more in USD).
Capped at ~$882k/month equivalent. Tokens come from TEDP reserve first; new tokens
only issued if reserve depleted.

**Discussion:** Decouples producer compensation from pure inflation. Prevents
overpaying when price is high and underpaying when price is low. The inverse power
curve is a nice touch. However, it creates a circular dependency (BPs report prices
that determine their own pay). For Anvo, consider using an external oracle or
median-of-BP-reports with outlier rejection. The TEDP "reserve first, issue second"
pattern is excellent — issue new tokens only when the treasury can't cover costs.

#### OIG-Weighted Voting (WAX)

WAX's Office of Inspector General scores each producer on quality metrics. The
system contract multiplies vote weight by the OIG score:

```
weighted_votes = total_votes × (guild_score / scaling_factor)
```

The OIG contract's code hash is validated before reading scores, preventing a
compromised external contract from manipulating elections.

**Discussion:** Separates quality assessment from the system contract. The
code-hash verification pattern is excellent security practice for any cross-contract
data read. However, it requires an independent assessment body (the OIG) to exist
and operate honestly. For Anvo, quality scoring could potentially be automated
(uptime, block production stats, finality participation) rather than requiring a
separate governance body.

### Account Name Blacklist (Vaulta — Hash-Reveal Pattern)

Vaulta implements a name reservation system that conceals which names are blocked:

- `denyhashadd(hash)` — stores a hash of the blocked name patterns (privileged)
- `denynames(patterns)` — activates the block only if `hash(patterns)` matches a stored hash
- `canon_name_t` class handles suffix and substring matching within EOSIO's 5-bit name encoding
- `undenynames(patterns)` — privileged removal

**Discussion:** Useful for preventing name squatting at launch. Reserve system
account names, brand names, or offensive names without revealing the full list
(which would tell squatters exactly what to target). The hash-reveal pattern
is clever — the blocked names are secret until activated. Consider for Anvo's
genesis.

### Token Supply Management (Vaulta — Practical Additions)

Vaulta added two actions to `eosio.token`:

- `issuefixed(to, supply, memo)` — issues only the delta between current circulating
  supply and target supply. Useful for deterministic supply targets.
- `setmaxsupply(issuer, maximum_supply)` — allows increasing (never decreasing
  below current supply) the max supply cap.

**Discussion:** Both are low-risk, high-utility additions. `issuefixed` prevents
accidental double-issuance. `setmaxsupply` enables governance to adjust the cap
without redeploying the token contract. Recommend including both.

## Producer Management

### Antelope Chain Findings

#### Self-Stake Requirement (UX Network)

UX requires producers to self-stake 10M UTX before registering. This is checked
against the `delband` table at `regproducer` time.

**Discussion:** Simple sybil resistance for producer registration. Ensures producers
have economic skin in the game. The specific amount should be governance-configurable
for Anvo rather than hardcoded.

## Effort

| Component | Effort |
|---|---|
| Fork and rebrand system contracts | 1 week (part of doc 07 rebrand) |
| Gas pricing + collection logic | 1-2 weeks |
| Baseline allocation on account creation | 1 week |
| Refundable deposit system | 3-4 weeks |
| RAM market replacement | 1-2 weeks |
| Granular permission system | 2-3 weeks |
| Referral tracking contract | 1-2 weeks |
| Governance contract (doc 21) | 3-4 weeks |
| Identity contracts (new) | 3-4 months (doc 13, Phase 2) |
| Boot sequence scripting | 1 week |
| Testing (all contract changes) | 2-3 weeks |
| **Total (launch-critical)** | **~3-4 months** |
| **Total (with identity)** | **~6-8 months** |
