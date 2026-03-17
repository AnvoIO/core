# Antelope Chain System Contract Comparison

## Overview

Analysis of system contract customizations across four Antelope-based chains:
WAX, Telos, UX Network, and Libre. Each chain forked from EOSIO and made
significant modifications. This document catalogs their innovations and
assesses relevance to Anvo Network.

## Chain Summary

| Chain | Focus | Key Innovations |
|---|---|---|
| **WAX** | NFTs, gaming | Voter rewards, fee-offset inflation, OIG-weighted voting, standby pay, WPS |
| **Telos** | Governance | Producer rotation/kick, inverse vote weighting, oracle-driven pay, no proxies |
| **UX Network** | Identity, economics | Usage-proportional inflation, oracle consensus, contract freeze, on-chain KYC |
| **Libre** | BTC integration | Permission system, referral tracking, BTC mining/staking, halving schedule |
| **Vaulta (EOS)** | Banking/finance | Inflation schedules, fee distribution router, RAM gifting, configurable REX, name blacklist |

---

## Feature-by-Feature Comparison

### Producer Management

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| Active producers | 21 | 21 (of 42) | 21 | 21 | 21 |
| Standby tier | Yes (configurable slots) | Yes (21 formal standbys) | No | No | No |
| Standby pay | Yes (time-weighted share) | Yes (half-share of actives) | No | No | No |
| Producer rotation | No | Yes (12hr round-robin) | No | No | No |
| Auto-kick underperformers | No | Yes (15% missed blocks, exponential penalty) | No | No | No |
| Geographic sort | Yes (by location) | Yes (ISO country code) | No | No | No |
| Quality scoring | Yes (OIG external scores) | No | No | No | No |
| Self-stake requirement | No | No | Yes (10M UTX) | No | No |

**Anvo relevance:**
- **Producer rotation** (Telos): Strong idea. Standbys get real block time, improving
  readiness and decentralization. The 12-hour cycle is elegant.
- **Auto-kick** (Telos): Essential for network health. The exponential backoff penalty
  (2^times_kicked hours) is well-calibrated.
- **OIG quality scoring** (WAX): Interesting separation of concerns — quality assessment
  by an external body, consumed by system contract with code-hash verification for security.
- **Self-stake requirement** (UX): Simple sybil resistance for producer registration.

### Voting

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| Proxy voting | Yes | **Disabled** | Yes | Yes | Yes |
| Vote decay | Standard time-weight | **Inverse vote weight (sine curve)** | Standard | Via power table | Time-weighted |
| Voter rewards | **Yes (40% of inflation)** | No | No | No | No |
| Min BPs to vote for rewards | Yes (configurable) | N/A | N/A | N/A | N/A |
| Weighted by external score | **Yes (OIG)** | No | No | No | No |
| Voting power source | Staked tokens | Staked tokens | Staked tokens | **Separate power table** | Staked tokens |

**Anvo relevance:**
- **Voter rewards** (WAX): Directly paying voters incentivizes governance participation.
  The continuous voteshare accrual model (rewards proportional to time staked × weight)
  is well-designed. The 40% allocation is aggressive but effective.
- **Inverse vote weighting** (Telos): The sine-curve formula `(sin(π×% - π/2) + 1)/2`
  elegantly incentivizes voting for more producers without adding complexity. Prevents
  single-producer voting cartels.
- **Proxy voting disabled** (Telos): Forces direct participation. Prevents vote-buying
  intermediaries. Tradeoff: reduces convenience for passive holders.
- **Separate power table** (Libre): Decouples voting power from raw stake. Allows
  power to factor in duration, identity level, behavior score.

### Producer Pay

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| Inflation rate | 5% (30/40/30 split) | Oracle-driven (decaying) | Usage-proportional | Custom | 5% (25/75 split) |
| Per-vote pay | **No (redirected to voters)** | No | No | No | Yes |
| Per-block pay | Yes | Yes (share-based) | Yes (equal split) | Yes | Yes |
| Fee offset | **Yes (fees reduce issuance, excess burned)** | No | No | No | No |
| Price oracle | No | **Yes (Delphi Oracle)** | **Yes (BP oracle consensus)** | No | No |
| Treasury funding | Yes (RNG oracle) | **Yes (TEDP multi-recipient)** | Yes (eosio.upay) | Yes (dao.libre) | No |
| Pay cap | No | **Yes (inverse power curve vs price)** | Yes (daily cap) | No | No |

**Anvo relevance:**
- **Fee-offset inflation** (WAX): Powerup fees reduce token issuance in real-time, with
  excess burned. Creates a path to zero or negative inflation during high activity. This
  aligns perfectly with Anvo's gas model — gas fees could similarly offset inflation.
- **Oracle-driven pay** (Telos): Decoupling pay from pure inflation and tying it to token
  price prevents both overpaying (when price is high) and underpaying (when price is low).
  The inverse power curve `378000 × (price/10000)^(-0.516)` is elegant.
- **Usage-proportional inflation** (UX): The most novel model. Inflation rewards users who
  actually use the chain, not just stakers. Creates a virtuous cycle. The entropy-inspired
  pricing function `C(x) = -x×ln(x)×e` is mathematically interesting.
- **Equal BP pay** (UX): All active producers get equal shares regardless of vote count.
  Reduces concentration incentive. Simpler.

### Resource Model

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| RAM pricing | Bancor | Bancor | **Fixed price (1 UTXRAM/KB)** | Bancor | Bancor |
| RAM fees | Standard | Standard | **Disabled** | Standard | 0.5% to REX |
| REX | Removed | Modified (no vote req) | Present | Removed | Standard |
| Powerup | Yes | No | No | No | Optional |
| Free baseline | No | No | No | **External tool** | No |

**Anvo relevance:**
- **Fixed-price RAM** (UX): Eliminates speculation. Already planned for Anvo (doc 11).
  UX validates this approach works in production.
- **REX removal** (WAX, Libre): REX adds complexity with limited benefit for chains
  that have alternative resource models. Anvo's gas model may make REX unnecessary.

### Governance & Proposals

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| On-chain WPS | **Yes (embedded in system)** | Via TEDP + external | No | **Yes (separate contract)** | No |
| Proposal lifecycle | Full (pending→vote→approve→execute) | External (Telos Decide) | No | Full (draft→active→voted→executed) | No |
| Treasury management | Savings + RNG fund | **TEDP (multi-recipient, price-adjusted)** | eosio.upay | dao.libre | Savings only |
| Proposal cost | No | Via external | No | Yes (50k SATS) | N/A |
| Vote counting | Standard | External | N/A | **Paginated (max_steps)** | N/A |

**Anvo relevance:**
- **WPS as separate contract** (Libre approach) is cleaner than embedding in system
  contract (WAX approach). Confirmed: Anvo should use standalone `core.gov` (doc 21).
- **TEDP multi-recipient treasury** (Telos): Configurable allocation percentages to
  multiple recipients with price adjustment. Good pattern for Anvo treasury.
- **Paginated vote counting** (Libre): Essential for scalability. Already noted in doc 21.

### Identity & Permissions

| Feature | WAX | Telos | UX | Libre | Standard EOSIO |
|---|---|---|---|---|---|
| On-chain KYC | No | No | **Yes (eosio.info)** | No | No |
| Granular permissions | No | No | No | **Yes (9 permission types, 5 states)** | No |
| Contract freeze | No | No | **Yes (eosio.freeze)** | No | No |
| Referral tracking | No | No | No | **Yes (libre-referrals)** | No |

**Anvo relevance:**
- **On-chain KYC framework** (UX): Clean design with multiple independent KYC providers,
  user-controlled data, composite key indexing. Good reference for Anvo's identity
  layer (doc 13). Key insight: separate user-provided keys from verification keys.
- **Contract freeze** (UX): Opt-in immutability for contracts. Simple trust mechanism
  for DeFi. Worth considering as a system-level feature.
- **Granular permissions** (Libre): Already planned for Anvo (doc 14). Libre validates
  the pattern.

### Unique Mechanisms

| Mechanism | Chain | Description |
|---|---|---|
| **Fee-burn deflation** | WAX | Excess fees beyond inflation needs are retired (burned) |
| **Exponential kick penalty** | Telos | 2^n hours penalty for nth kick, scales punishment |
| **Sine-curve vote weight** | Telos | `(sin(π×%−π/2)+1)/2` incentivizes voting for more BPs |
| **Entropy inflation curve** | UX | `C(x)=−x×ln(x)×e` — inflation inversely proportional to utilization |
| **Oracle modal hash** | UX | BPs report usage data, statistical mode determines consensus |
| **Usage-proportional rewards** | UX | Users who use the chain get inflation tokens proportional to CPU |
| **Halving emission** | Libre | 6-month halving over 10 years for mining rewards |
| **Variant state machines** | Libre | Multi-phase processing across transactions to avoid CPU limits |
| **Code-hash verification** | WAX | System contract validates external contract code hash before trusting data |

---

## Recommendations for Anvo Network

### Adopt (high confidence)

1. **Fee-offset inflation** (WAX) — Gas fees reduce token issuance. Excess burned.
   Natural fit with Anvo's gas model.

2. **Producer rotation** (Telos) — 12-hour rotation cycle between active and standby.
   Improves decentralization and standby readiness.

3. **Auto-kick with exponential penalty** (Telos) — Remove underperforming BPs
   automatically. Essential for network reliability.

4. **Fixed-price RAM** (UX) — Already planned. UX validates it works.

5. **Paginated batch processing** (Libre) — `max_steps` pattern for vote counting
   and any mass-update operations.

6. **Contract freeze** (UX) — Opt-in immutability. Simple to implement, high trust value.

### Adapt (needs Anvo-specific design)

7. **Voter rewards** (WAX) — Good incentive but 40% of inflation is aggressive.
   Calibrate for Anvo's tokenomics. Tie to identity level for sybil resistance.

8. **Inverse vote weighting** (Telos) — The sine-curve formula is elegant. Consider
   combining with Anvo's identity system (higher identity = more vote weight).

9. **On-chain identity framework** (UX) — Good structural reference for doc 13.
   Adapt the multi-provider, user-controlled data pattern.

10. **Oracle-driven pay** (Telos/UX) — Useful concept but creates circular dependency
    (BPs report prices that determine their pay). Consider using external oracle
    integration rather than self-reported data.

11. **Governance as separate contract** (Libre) — Confirmed approach for `core.gov`.
    WAX's embedded WPS is too monolithic.

### Study Further

12. **Usage-proportional inflation** (UX) — Most novel model. The entropy-inspired
    curve is interesting but complex. Worth deeper analysis for Anvo's long-term
    tokenomics, especially combined with the gas model.

13. **TEDP multi-recipient treasury** (Telos) — Configurable allocation with price
    adjustment. Good foundation for Anvo treasury design.

14. **OIG quality scoring** (WAX) — External quality assessment with code-hash
    verification. Interesting governance mechanism but requires an independent
    assessment body to exist.

### Adopt from Vaulta (EOS)

15. **Inflation schedule system** (Vaulta) — `schedules` table with pre-committed
    `(start_time, continuous_rate)` pairs. Governance can pre-commit to inflation
    changes (halvings, rate adjustments) without future MSIGs. `execschedule()`
    callable by anyone once the time arrives. Simple, powerful.

16. **Fee distribution router** (Vaulta `eosio.fees`) — Epoch-based strategy system
    with configurable weights for multiple destinations (staking rewards, RAM burn,
    BP pay, treasury). Anyone can call `distribute()` after each epoch. The single-
    contract routing pattern is cleaner than WAX's direct fee-offset approach.

17. **RAM gift system** (Vaulta) — Encumbered RAM that can only be returned to the
    gifter, not sold/traded. Perfect for dApp-sponsored account onboarding: gift RAM
    to new users without risk of them extracting value. The `gifted_ram` table tracks
    giftee→gifter→bytes.

18. **Max supply awareness in inflation** (Vaulta) — When `supply + new_tokens >
    max_supply`, gracefully fall back to using existing reserves instead of minting.
    Enables transition from inflationary to fixed-supply economics.

19. **`issuefixed` and `setmaxsupply`** (Vaulta eosio.token) — `issuefixed` issues
    only the delta to reach a target supply. `setmaxsupply` allows increasing (never
    decreasing) the cap. Practical supply management tools.

20. **Account name blacklist with hash-reveal** (Vaulta) — Reserve names without
    revealing which ones. `denyhashadd(hash)` stores a hash; `denynames(patterns)`
    activates only if patterns match a stored hash. Useful for launch name squatting
    prevention.

### Adapt from Vaulta

21. **Configurable REX maturity** (Vaulta) — `setrexmature()` makes lockup periods
    governance-adjustable (1-30 buckets) rather than hardcoded. If Anvo retains any
    staking lockup, this pattern is better than hardcoding.

22. **Peer keys** (Vaulta) — On-chain P2P authentication keys for producers.
    `regpeerkey`/`delpeerkey` actions with versioned table structure. Minor feature
    but improves network security.

23. **Separate `eosio.bpay`** (Vaulta) — Dedicated BP pay contract with equal
    distribution among top N. Consider hybrid: equal base + small performance bonus.

### Skip

- **Dual-token swap architecture** (Vaulta `core.vaulta`) — Migration artifact for
  EOS→$A rebrand. New chain launches with one native token.
- **Mandatory sender gating** (Vaulta `get_sender()` checks) — Migration artifact.
- **`unvest` B1 clawback** (Vaulta) — EOS-specific, not applicable.
- **WPS embedded in system contract** (WAX) — Too monolithic. Use separate contract.
- **Genesis vesting/GBM** (WAX) — Specific to WAX's launch. Not applicable.
- **Delphi Oracle** (Telos) — Full oracle is out of scope for system contracts.
  Use lighter integration if needed.
- **Proxy voting disabled** (Telos) — Too restrictive for launch. Can be governance-
  configurable later.
