# Governance & DAO Contract

## Overview

A standard governance/DAO contract should be part of the core system contracts.
This enables on-chain proposal creation, voting, and execution for protocol
parameter changes, treasury management, and community decisions.

No existing Anvo research doc covers this. It is a new addition to the roadmap.

## Libre Chain Reference: btc-libre-governance

Libre implemented a governance contract with the following characteristics:

### Proposal Lifecycle

```
DRAFT → ACTIVE → VOTED → APPROVED/REJECTED → EXECUTED
```

1. **DRAFT**: Creator submits proposal (requires 1000 LIBRE minimum balance)
2. **ACTIVE**: Payment of 50,000 SATS moves proposal to active voting
3. **VOTED**: 10-day voting window opens
4. **APPROVED/REJECTED**: Determined by vote tally:
   - >50% of votes must be FOR
   - ≥10% of circulating supply must participate (quorum)
5. **EXECUTED**: Approved proposals trigger fund transfer from treasury

### Voting Mechanism

- Voting power derived from `stake.libre` power table (staked tokens, not liquid)
- Power queried at **vote-counting time**, not at proposal creation — prevents
  flash-vote attacks where someone stakes just before a vote
- Paginated vote counting via `max_steps` parameter to avoid hitting transaction
  CPU limits with large voter populations
- `countvotes` action iterates through vote table incrementally
- State table tracks vote-counting progress across multiple transactions
- Supports `checkvotes` for live vote status during voting period

### Notable Patterns

1. **Paginated batch processing** — essential for any operation that may touch
   thousands of records. The `max_steps` pattern (process N records per call,
   track progress in a singleton) is reusable across many contracts.

2. **Per-proposal vote scoping** — separate vote tables scoped to each proposal,
   not a global vote table. More gas-efficient for lookups.

3. **Separate approval role** — the person who approves a passed proposal is not
   the proposer. Separation of concerns.

4. **Proposal cost as anti-spam** — requiring payment to activate a proposal
   prevents frivolous proposals. The cost can be refunded for approved proposals.

## Other DAO Models to Study

Before finalizing the Anvo governance design, research these existing approaches:

### Aragon (Ethereum)
- Modular DAO framework with plugin architecture
- Token voting, optimistic governance, multisig modes
- Separation of proposal creation, voting, and execution plugins
- Worth studying: plugin architecture for extensibility

### Compound Governor (Ethereum)
- Widely adopted, battle-tested governor contract
- Proposal → voting delay → voting period → timelock → execution
- Delegation: token holders delegate voting power to representatives
- Quorum and proposal thresholds as governance parameters
- Worth studying: delegation model, timelock safety

### Snapshot (Off-Chain + On-Chain Execution)
- Off-chain voting (gasless), on-chain execution via Safe
- Multiple voting strategies (token-weighted, quadratic, whitelist)
- Worth studying: voting strategy abstraction

### Tally / OpenZeppelin Governor
- Standardized governor interface (ERC-5114, ERC-6372)
- Modular: voting delay, voting period, quorum, timelock all configurable
- Clock abstraction (block number or timestamp)
- Worth studying: standardized interface for tooling compatibility

### WAX Worker Proposal System (On-Chain, Embedded)
- Full proposal lifecycle embedded in the system contract
- Proposers register with profile (name, bio, country, LinkedIn)
- Committees appoint reviewers to evaluate proposals
- Funding from `eosio.saving` in installments (funding_goal / total_iterations)
- Same voting power as producer elections (stake2vote)
- Worth studying: installment-based funding, committee/reviewer roles
- **Warning:** Embedding WPS in system contract makes it too monolithic. Use separate contract.

### Telos Economic Development Plan (TEDP)
- Multi-recipient treasury with configurable allocations
- Recipients include: foundation, core dev, grants, fuel fund, REX stakers
- Price-adjusted payouts (at TLOS >= $1, REX payout drops to 2/3)
- Funds come from reserve first; new tokens only issued if reserve depleted
- Worth studying: multi-recipient treasury, price-adjusted payouts

### Cosmos SDK Governance
- Deposit period → voting period → tallying
- Deposits from multiple accounts to reach threshold (crowdfunded proposals)
- Weighted vote: yes/no/abstain/no-with-veto (4-way vote)
- Veto power: >33% no-with-veto kills proposal regardless of yes votes
- Worth studying: deposit crowdfunding, veto mechanism

### Snapshot + Reality.eth (Optimistic Governance)
- Proposals pass automatically unless challenged
- Challengers must post a bond; winner keeps loser's bond
- Dramatically reduces governance overhead for routine decisions
- Worth studying: optimistic execution for low-stakes governance

## Antelope Chain Findings: Governance Patterns

### Treasury Distribution (Telos TEDP)

Telos implements a multi-recipient treasury (TEDP) with configurable allocations:

| Recipient | Purpose | Daily Cap |
|---|---|---|
| Telos Foundation | Operations | ~$23k |
| Core Development | Engineering | ~$13k |
| Ignite Grants | Community grants | ~$16k |
| Fuel Fund | User onboarding | ~$55k |
| REX Stakers | Staking rewards | ~$1.1k/30min |

Key patterns:
- **Price-adjusted payouts** — at TLOS ≥ $1, REX payout drops to 2/3; at ≥ $2, drops to 1/3
- **Reserve first, issue second** — tokens come from TEDP reserve account before minting new ones
- **Configurable recipients** — payout table can be updated via governance

**Discussion:** The multi-recipient treasury with price adjustment is a good model
for Anvo. Rather than a single `eosio.saving` bucket, allocate treasury funds to
specific purposes with governance-adjustable percentages. The "reserve first" pattern
prevents unnecessary inflation when the treasury has sufficient funds.

### Worker Proposals (WAX — Embedded vs Standalone)

WAX embeds the full WPS in the system contract with proposers, committees, reviewers,
and installment-based funding. The proposal lifecycle includes profile registration,
committee review, community voting, and iterative payouts.

**Discussion:** WAX proves the concept works but the embedded approach makes the
system contract too complex. The installment-based funding pattern (funding_goal /
total_iterations, claimed per interval) is worth adopting in the standalone `core.gov`
contract. The committee/reviewer role separation is interesting but may be premature
for launch — start with simple community voting and add structured review later.

### Vote Counting at Claim Time (WAX + Libre)

Both WAX and Libre query voting power at count time, not at proposal creation time:
- WAX: `stake2vote()` recalculated when votes are tallied
- Libre: power table queried during `countvotes` pagination

This prevents flash-vote attacks where someone acquires tokens, votes, then sells.

**Discussion:** Essential. Anvo's governance contract must snapshot voting power
at tally time or use a time-weighted average. The pagination pattern (Libre's
`max_steps`) handles the performance concern of querying power for all voters.

## Design Considerations for Anvo

### Vaulta Inflation Schedule (Pre-Committed Governance)

Vaulta's `schedules` table stores `(start_time, continuous_rate)` pairs that
auto-execute when the time arrives. This allows governance to pre-commit to
tokenomics changes without future MSIGs.

**Discussion:** This effectively automates one of the most important governance
decisions (inflation rate). Rather than requiring ongoing proposals to adjust
inflation, the trajectory is set upfront and executes automatically. The governance
contract should have the authority to modify the schedule, but the default path
should be pre-committed at launch.

### Vaulta Fee Router (Strategy-Based Distribution)

Vaulta's `eosio.fees` contract distributes fees based on weighted strategies:
`buyramburn`, `donatetorex`, `eosio.bpay`, `eosio.bonds`. Weights are governance-
adjustable.

**Discussion:** Fee distribution strategy is inherently a governance decision.
The governance contract should have authority to adjust fee router weights via
proposals. This is a concrete use case for the governance system at launch —
the community votes on how fees are distributed.

### Must-Haves (Launch)

1. **Proposal lifecycle** — create, vote, execute with clear state machine
2. **Voting power from staking** — aligned with staking system (doc 14)
3. **Quorum requirements** — minimum participation threshold
4. **Proposal cost / deposit** — anti-spam with refund on approval
5. **Paginated vote counting** — essential for scalability
6. **Timelock on execution** — safety delay between approval and execution
7. **Parameter governance** — gas prices, deposit amounts, baseline allocations

### Should-Haves (Near-Term)

1. **Delegation** — token holders delegate voting power to trusted representatives
2. **Multiple vote types** — yes/no/abstain at minimum; consider no-with-veto
3. **Treasury management** — proposals can request funds from a DAO treasury
4. **Identity-gated participation** — require minimum identity level (doc 13)
   to create proposals or vote (prevents sybil governance attacks)

### Could-Haves (Future)

1. **Quadratic voting** — square root of voting power, requires proof of personhood
2. **Optimistic governance** — routine decisions auto-pass unless challenged
3. **Sub-DAOs** — nested governance for specific domains (technical, grants, etc.)
4. **Conviction voting** — voting power increases over time for sustained support
5. **Rage quit** — minority protection: disagreeing voters can exit with their share

### Integration Points

| System | Integration |
|---|---|
| Staking (doc 14) | Voting power derived from stake table |
| Identity (doc 13) | Proposal creation and voting gated by identity level |
| Permissions (doc 14) | `propose` capability flag gates who can create proposals |
| Referrals (doc 14) | Referrer reputation could weight governance participation |
| Resource model (doc 11) | Gas prices and baseline allocations governed via proposals |
| Treasury | DAO contract manages a treasury account for fund disbursement |

## Implementation

### Contract Architecture

```
core.gov (governance contract)
├── Proposals table (proposal_id, creator, title, description, status, dates)
├── Votes table (scoped per proposal: voter, vote, weight)
├── Config singleton (quorum, voting_period, timelock, proposal_deposit)
├── State singleton (vote counting progress for pagination)
└── Actions:
    ├── propose(creator, title, description, actions[])
    ├── activate(proposal_id)  — pays deposit, starts voting
    ├── vote(voter, proposal_id, vote_type)
    ├── countvotes(proposal_id, max_steps)  — paginated tallying
    ├── execute(proposal_id)  — after timelock, runs proposed actions
    ├── cancel(proposal_id)  — creator cancels before voting starts
    └── setconfig(params)  — governance parameters (requires governance vote)
```

### Effort

| Component | Effort |
|---|---|
| Core governance contract | 2-3 weeks |
| Voting power integration with staking | 1 week |
| Paginated vote counting | 1 week |
| Timelock and execution | 1 week |
| Treasury integration | 3-5 days |
| Testing | 1-2 weeks |
| **Total** | **~6-8 weeks** |

## Open Questions

1. Should the governance contract be a single monolithic contract or a modular
   plugin-based system (like Aragon)?
2. What voting strategies to support at launch? Token-weighted only, or also
   quadratic (requires identity)?
3. Should delegation be at the protocol level (like Cosmos) or contract level?
4. How should the timelock interact with the existing `core.msig` multisig?
   Should governance proposals go through msig, or be a separate execution path?
5. What is the minimum identity level required for governance participation?
