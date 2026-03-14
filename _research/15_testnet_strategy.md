# Testnet Strategy

## Phased Approach

Launch testnet in stages, each proving a layer of the stack before adding the next.

## Phase T1: Internal Testnet — "Does the Fork Work?" (Week 1-2 of dev)

**Goal:** Prove the rebranded node boots, produces blocks, and passes basic tests.

**What ships:**
- Rebranded node binary (new executable names, new namespace)
- Existing system contracts (rebranded, no new features)
- Standard token contract (core.token) with $ANVO core token symbol
- Genesis with new system account names (if account switch is ready)
- 3-node local testnet (1 producer + 2 peers)

**Success criteria:**
- [ ] Node compiles on x86_64
- [ ] Genesis boots with new system account names
- [ ] Block production is stable
- [ ] Token transfers work
- [ ] Account creation works
- [ ] Basic contract deployment works
- [ ] Existing unit tests pass (with renamed accounts)

**Who uses it:** Just us.

## Phase T2: Feature Testnet — "Does the UX Thesis Work?" (Month 2-3)

**Goal:** Prove the new resource model and passkey accounts work end-to-end.

**What ships (additive):**
- Gas payment path (parallel to staking)
- Free baseline allocation
- Refundable account deposit
- Passkey (WebAuthn) account creation
- Hash-based account names
- Updated system contract with gas pricing

**Success criteria:**
- [ ] Create account with passkey via web browser
- [ ] Transact with gas payment (no staking)
- [ ] Transact with staking (no gas)
- [ ] Baseline allocation covers casual usage
- [ ] Deposit locks on creation, refunds after maturity
- [ ] dApp can sponsor account creation + gas for users
- [ ] All above works on mobile (iOS Safari, Android Chrome)

**Who uses it:** Us + a few trusted developers for feedback.

## Phase T3: Infrastructure Testnet — "Can We Run This?" (Month 3-4)

**Goal:** Prove operational readiness — AArch64, monitoring, snapshots.

**What ships (additive):**
- AArch64 node binary (if port is ready)
- Prometheus monitoring integration
- Snapshot creation and restoration verified
- State history plugin configured and streaming
- Basic indexer consuming state history
- Bridge testnet (Crosslink pointed at this chain)

**Success criteria:**
- [ ] Node runs on AWS Graviton (AArch64)
- [ ] Performance within 15% of x86_64
- [ ] Snapshots restore correctly on both architectures
- [ ] State history streams to external consumer
- [ ] Crosslink bridge connects to BTC/ETH testnets
- [ ] Bridge peg-in/peg-out completes end-to-end

**Who uses it:** Us + bridge operators.

## Phase T4: Public Testnet — "Build On This" (Month 4-6)

**Goal:** Open to external developers. Prove the platform is buildable.

**What ships (additive):**
- TypeScript SDK with passkey + gas support
- Public RPC endpoints
- Block explorer (fork or deploy existing)
- Faucet for testnet tokens
- Identity contracts (at least social vouching)
- Developer documentation
- Contract deployment guide
- Reference applications (simple token swap, governance vote)

**Success criteria:**
- [ ] External developer can create account + deploy contract without assistance
- [ ] SDK documentation covers passkey flow end-to-end
- [ ] Block explorer shows transactions, accounts, contract tables
- [ ] At least 3 external teams building on the testnet
- [ ] Faucet distributes tokens without manual intervention
- [ ] Identity social vouching flow works

**Who uses it:** Open to anyone.

## Phase T5: Stress Testnet — "Can It Handle Load?" (Month 8-10)

**Goal:** Prove parallel execution and throughput under load.

**What ships (additive):**
- Block-STM parallel execution (if ready)
- Load testing framework
- Transaction generators (token transfers, contract calls, mixed workloads)
- Performance benchmarks published

**Success criteria:**
- [ ] 4x+ throughput improvement with Block-STM vs sequential
- [ ] No consensus failures under sustained load
- [ ] Finality remains <3 seconds under load
- [ ] No state corruption after millions of transactions
- [ ] Benchmark numbers published for community review

**Who uses it:** Us + performance-focused community members.

## Mainnet Readiness Criteria

Before mainnet launch, ALL of the following:

- [ ] All Phase T1-T5 success criteria met
- [ ] Security audit completed (node + system contracts + bridge)
- [ ] At least 1 existing EOSIO chain has tested snapshot migration
- [ ] Bridge operational with real BTC/ETH testnets
- [ ] At least 10 external developers have deployed contracts
- [ ] 30+ days of uninterrupted public testnet operation
- [ ] Tokenomics finalized and community-reviewed
- [ ] Governance model documented and tested
- [ ] Emergency procedures documented (chain halt, recovery, upgrade)
- [ ] At least 5 independent node operators running testnet validators

## Timeline Summary

```
Month 1-2:   T1 (internal) ← rebrand + boot
Month 2-3:   T2 (features) ← gas + passkeys
Month 3-4:   T3 (infra)    ← ARM + bridge + monitoring
Month 4-6:   T4 (public)   ← SDK + docs + external devs
Month 8-10:  T5 (stress)   ← Block-STM + load testing
Month 10-12: Audits + migration testing
Month 12-14: Mainnet prep + launch
```
