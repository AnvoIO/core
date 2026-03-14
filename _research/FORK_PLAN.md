# Fork Plan: Anvo Network

## Vision

Build Anvo Network, a new Layer 1 blockchain that:
- Provides existing EOSIO chains (EOS, Telos, WAX, UX, etc.) a viable upgrade path
- Maintains full smart contract compatibility — no recompilation, no redeployment
- Closes the competitive gaps against modern L1s (Aptos, Sui, Solana)
- Runs on both x86_64 and AArch64 (ARM) from day one

## Starting Point

Fork of [Spring](https://github.com/AntelopeIO/spring) (AntelopeIO), the most current
C++ implementation of the Antelope protocol with Savanna (HotStuff-based BFT) consensus.

**What Spring gives us:**
- Production-proven blockchain node (years of mainnet operation across multiple chains)
- Savanna consensus with ~2-3 second finality via BLS12-381 quorum certificates
- WASM smart contract runtime with JIT/AOT compilation
- Decoupled proposer/finalizer architecture
- Plugin-based extensibility (22+ plugins)
- REST API infrastructure
- Comprehensive test suite (~870 C++ files, 70+ Python integration tests)

## Competitive Position

### Where Spring/Savanna Is Already Strong
- Finality: ~2-3s (faster than Ethereum, Solana, Cosmos)
- WASM contracts: mature tooling, C++ smart contracts
- Resource model: flexible CPU/NET/RAM staking (no gas fees)
- Proposer/finalizer separation: architecturally clean (Ethereum still working toward this)
- Accountable safety: BLS-based finality with explicit QCs

### Gaps to Close
1. **No parallel transaction execution** — single-threaded WASM, ~4-8K TPS ceiling
2. **x86_64 only** — no ARM server support (20-40% cheaper compute on AWS/Azure)
3. **No native cross-chain messaging** — unlike Cosmos (IBC) or Polkadot (XCMP)
4. **Ecosystem contraction** — developer tooling and community have shrunk since 2018
5. **Sub-second finality** — Sui (~400ms) and Aptos (~900ms) have pushed further

### Unique Advantage
**EOSIO compatibility.** No other chain can offer existing EOSIO projects an upgrade path
to modern BFT consensus, parallel execution, and multi-architecture support. Thousands of
deployed contracts work unmodified.

---

## Technical Roadmap

### Phase 1: Foundation & Core Features (Months 1-4)

Everything needed to boot the chain with its differentiating features.
Gas model, passkey accounts, and baseline allocation are Phase 1 because
testnet T2 (Month 2-3) depends on them.

#### 1A. Fork, License & Rebrand (~2-3 weeks)
**Goal:** Fork under BSL, remove all legacy branding, establish new project identity.

**License: Business Source License 1.1**
- Source-available from day one. Converts to Apache 2.0 after 3 years per version.
- Permitted: running nodes, building dApps, migrating EOSIO chains, private chains,
  research, education — everything except launching a competing public L1.
- Protects our engineering work (Block-STM, gas model, AArch64 port, indexer, identity)
  while allowing full ecosystem use.
- Contributors sign off via DCO (Developer Certificate of Origin).
- Legal basis: Spring is MIT-licensed (Nov 2025), MIT permits sublicensing under BSL.
- **Detailed plan:** [18_licensing.md](18_licensing.md)

**Rebrand:** Remove all references to eosio, EOSIO, EOS, antelope, AntelopeIO, spring,
Spring, leap, Leap, and block.one from the codebase. Replace with new project identity.

**Scale:** ~11,000+ occurrences across ~500+ files, 35 directories to rename.

**Strategy:** Absorb key submodules first (eliminates cross-boundary issues),
then rebrand in one scripted pass. See naming table and submodule strategy above.

**Also required:**
- New genesis configuration
- Establish CI/CD pipeline
- New project repository and organization

#### 1B. System Contracts
**Goal:** Fork and modify the bundled system contracts for the new chain.

**Starting point:** Spring's `unittests/contracts/` — complete, tested contracts
with Savanna support (eosio.system, eosio.token, eosio.msig, eosio.wrap, eosio.boot).

**Modifications for launch (Phase 1):**
- Rename accounts (part of rebrand)
- Add gas pricing + collection (doc 11)
- Add baseline allocation on account creation (doc 11)
- Add refundable deposit system (doc 11)
- Replace Bancor RAM market with fixed pricing (doc 11)
- Hash-based account name derivation (doc 12)
- Boot sequence scripting for new chain genesis

**New contracts (Phase 3):**
- Identity registry + social vouching (doc 13)
- Attestation framework (doc 13)
- Social recovery (doc 12)
- Name service (doc 12)
- Data vault (doc 13)

**Detailed plan:** [14_system_contracts.md](14_system_contracts.md)

#### 1C. Resource Model: Gas + Staking + Baseline (~2-3 months)
**Goal:** Eliminate the onboarding cliff while preserving free-transaction capability.

**Model: Baseline + Staking + Parallel Gas**

Two parallel paths to the same resources, chosen per-transaction:

- **Staking path** (unchanged from today): stake tokens → get bandwidth allocation → free
- **Gas path** (new): pay per-transaction at governance-set prices → no staking needed

Plus:
- **Free baseline allocation** for every account (enough for casual daily use)
- **RAM market replaced** with governance-set pricing (no more Bancor speculation)
- **Refundable account deposit** with maturity conditions (anti-spam)

**How it works:**
- New optional `gas_payer` field on transactions (protocol feature)
- If set: resources billed at gas price, deducted from payer's token balance
- If not set: existing staking model applies unchanged
- dApps can sponsor gas for their users (gas_payer = contract account)
- Both paths coexist on the same chain

**Legacy EOSIO impact: zero breaking changes.**
- Protocol feature is opt-in (chains activate when ready)
- All existing transactions, contracts, and staking work unchanged
- Gas path is additive — new capability, nothing removed

**Unique competitive position:** No other L1 lets users choose between free
(staked) and paid (gas) transactions. Every other chain is gas-only.

**Detailed plan:** [11_resource_model_evolution.md](11_resource_model_evolution.md)

#### 1D. Passkey Accounts (~2-3 months, parallel with 1C)
**Goal:** Users create accounts with Face ID / fingerprint. No seed phrases, no wallet
apps, no browser extensions. The device IS the wallet. The biometric IS the key.

**What already exists in Spring:**
- WebAuthn (WA) key type with full P-256 signature verification (40+ tests)
- Permission system: hierarchical, weighted thresholds, mixed key types (K1+R1+WA)
- Key rotation via `updateauth`, hierarchical recovery (owner→active)

**What needs to be built:**
- Permissionless account creation with gas payment (depends on 1C)
- Hash-based account name derivation from passkey public key
- TypeScript SDK with WebAuthn integration
- Reference auth UI (passkey creation, device management, recovery)
- Social recovery contract (guardian-based, time-delayed)
- Name service contract (optional human-readable aliases)

**5-second onboarding:** Visit dApp → "Create Account" → Face ID → done.

**Competitive edge:** No other L1 has this at the protocol level. Ethereum
approximates it with ERC-4337 smart wallets (contract-level, fragmented standards,
expensive deployment). Every account is natively a smart wallet.

**Detailed plan:** [12_passkey_accounts.md](12_passkey_accounts.md)

#### 1E. LLVM Modernization (Prerequisite for OC + AArch64)
**Goal:** Update eos-vm-oc to build with modern LLVM (14+).

**Problem found:** Spring requires LLVM 7-11 for eos-vm-oc (the optimized WASM
compiler). Ubuntu 24.04 ships LLVM 14-20. Ubuntu 22.04 has LLVM 11 but is EOL
April 2027. The LLVM 7-11 requirement blocks builds on modern Linux and must be
resolved before the AArch64 port (which depends on LLVM's AArch64 backend).

**What needs to change:**
- `CMakeLists.txt` version check: `LLVM_VERSION_MAJOR` range 7-11 → 7-20
- `LLVMEmitIR.cpp`: 3 conditional blocks for LLVM <9, <10, ==10 API differences
- `LLVMJIT.cpp`: 2 conditional blocks for LLVM 7 and <10 API differences
- LLVM API migrations: deprecated pass manager APIs, type system changes,
  ORCv1→ORCv2 JIT layer (if targeting LLVM 16+)

**Build strategy (phased):**
1. Build without OC (`-DENABLE_OC=OFF`) — interpreter + JIT only (verified working)
2. Patch for LLVM 14 — smallest API delta from LLVM 11, available on Ubuntu 22.04+
3. Patch for LLVM 16-18 — full modern Linux support
4. Then proceed with AArch64 port (LLVM handles AArch64 codegen natively)

**Effort:** ~1-2 weeks for LLVM 14, ~2-3 weeks for LLVM 16+

#### 1F. AArch64 Support (~3 months, parallel with 1C/1D, depends on 1E)
**Goal:** Production-quality ARM server support for eos-vm-oc runtime.

**Strategy:** Port eos-vm-oc from x86_64 to AArch64 using dedicated register X28
as replacement for x86_64 GS segment register. This is the approach used by V8,
SpiderMonkey, and Wasmtime on ARM.

**Depends on:** 1E (LLVM modernization) — LLVM must support AArch64 backend,
which all modern LLVM versions do natively.

**Key changes (10 files requiring modification):**

| Component | Change | Effort |
|-----------|--------|--------|
| `gs_seg_helpers.h/c` | Abstract GS→X28, architecture macros | 1 week |
| `LLVMEmitIR.cpp` | Replace `address_space(256)` with X28 base register | 3-4 weeks |
| `switch_stack_linux.s` | New AArch64 stack switching assembly | 1 day |
| `executor.cpp` | Signal handler reads X28 from ucontext | 1 week |
| `memory.hpp/cpp` | Runtime page size detection (4KB/64KB) | 1 week |
| `CMakeLists.txt` | AArch64 detection, `-ffixed-x28` flag | 1 day |
| `city.cpp` | ARM CRC32 intrinsic path | 1 day |
| `secp256k1` | C fallback (automatic, no changes) | — |

**No contract changes required.** WASM is the abstraction boundary — contracts are
architecture-neutral bytecode.

**Detailed plan:** [06_aarch64_port_implementation.md](06_aarch64_port_implementation.md)

**Expected performance:** 85-105% of x86_64 on AWS Graviton4.

#### 1F. Testnet Strategy
**Goal:** Phased testnet proving each layer before adding the next.

| Phase | When | What | Who |
|---|---|---|---|
| T1: Internal | Month 1-2 | Rebranded node + existing contracts (no OC) | Us |
| T2: Features | Month 2-3 | Gas + passkeys + baseline + LLVM modernization | Us + trusted devs |
| T3: Infra | Month 3-4 | ARM + OC runtime + bridge + monitoring | Us + bridge operators |
| T4: Public | Month 4-6 | SDK + docs + explorer + faucet | Open |
| T5: Stress | Month 8-10 | Block-STM + load testing | Open |

**Mainnet readiness:** Security audit, 30+ days uninterrupted testnet, migration
tested with real chain snapshot, 5+ independent validators.

**Detailed plan:** [15_testnet_strategy.md](15_testnet_strategy.md)

---

### Phase 2: Parallel Execution (Months 3-9)

**Goal:** Block-STM parallel transaction execution for 4-8x throughput improvement.

**Confirmed approach: Block-STM** (optimistic parallel execution with conflict detection).

#### Why Block-STM

Four parallel execution strategies were evaluated:

| Strategy | Throughput | Protocol Change | Contract Change | Proven |
|----------|-----------|----------------|-----------------|--------|
| **Block-STM** | **4-8x** | **None** | **None** | **Yes (Aptos)** |
| Scope Partitioning | 2-4x | Possibly | Possibly | No (never shipped) |
| Read-Only Parallelism | 2-3x (reads only) | None | None | Yes (trivial) |
| State Sharding | Near-linear | Major | Likely | Partially |

Block-STM was selected because:
1. **Zero breaking changes** — blocks, receipts, Merkle roots are identical to sequential execution.
   This is non-negotiable for the EOSIO upgrade path. Existing contracts run unmodified.
2. **Proven in production** — Aptos shipped it, published the research, demonstrated 4-16x
   gains on real workloads with 32 threads.
3. **Best fit for EOSIO workloads** — most blocks contain transactions touching different
   accounts and contracts. Low natural conflict rate = high parallelism.
4. **WASM layer is already ready** — per-thread executors, memory, allocators exist. The
   work is in the state layer (shadow state, scheduler), not the VM.
5. **Doesn't touch ChainBase** — shadow state sits above it, avoiding the riskiest refactor.
6. **Graceful degradation** — worst case (all conflicts) = sequential with small overhead.
   Ship with feature flag, zero downside risk.

Scope partitioning was the original EOSIO whitepaper vision but was never implemented.
It breaks down for real DeFi transactions that span multiple scopes (e.g., a DEX trade
touches the DEX contract scope AND two token contract scopes). Block-STM handles these
naturally through conflict detection and re-execution.

State sharding was ruled out — the engineering cost is 12-24 months, it requires protocol
changes, and cross-shard composability remains an unsolved problem industry-wide.

#### Architecture

```
Block (N transactions)
        |
   Scheduler (deterministic ordering)
        |
   +---------+---------+---------+
   |         |         |         |
Worker 0  Worker 1  Worker 2  Worker 3
   |         |         |         |
shadow_0  shadow_1  shadow_2  shadow_3
   |         |         |         |
   +----+----+----+----+----+----+
        |              |
  Multi-Version    Conflict
  Data Structure   Detector
        |              |
   Commit (in tx order) / Abort+Retry
```

#### Implementation Phases

**Phase 2A: Read-Only Parallelism — Warmup (Month 3, ~2-4 weeks)**
- Separate read-only queries from state-modifying transactions
- Execute read-only transactions in parallel against a state snapshot
- Gets the team familiar with Spring's threading model
- Delivers immediate, low-risk value
- Foundation infrastructure reused by Block-STM (thread pools, per-thread contexts)

**Phase 2B: Deferred Sequence Assignment (Month 3-4, ~2-3 weeks)**
- Remove the `global_action_sequence` bottleneck (100% conflict rate today)
- Execute transactions without assigning sequences
- Assign `global_sequence`, `recv_sequence`, `auth_sequence` in a deterministic
  post-execution pass based on transaction index
- **This alone unblocks all other parallelization work**
- Can be developed and tested in isolation against sequential execution
  (verify identical receipts)

**Phase 2C: Multi-Version Store + Shadow State Layer (Month 4-6, ~6-8 weeks)**
- Multi-version data structure indexed by `(state_key, tx_index, incarnation)`
- Per-transaction shadow state overlay between `apply_context` and ChainBase
- Each worker thread gets its own `apply_context` + `transaction_shadow_state`
- Database operations (`db_store_i64`, `db_get_i64`, etc.) route through shadow state
- Read path: own writes → multi-version store → ChainBase base state
- Write path: buffer in shadow state, commit to ChainBase in order
- State keys: `(table_id, primary_key)` for contract state,
  `account_name` for system state
- Both components can be developed in parallel and tested independently

**Phase 2D: Scheduler & Conflict Detection (Month 6-7, ~4-6 weeks)**
- Worker threads execute transactions optimistically
- Read/write sets tracked per transaction
- Validation pass checks that all reads are still valid
- Aborted transactions re-execute with updated state
- Deterministic ordering ensures identical results across nodes

**Phase 2E: Controller Integration & Tuning (Month 7-9, ~4-6 weeks)**
- Wire Block-STM into `controller_impl` block production path
- Feature flag for sequential fallback
- Parallel validation for non-producing nodes
- Performance tuning (thread count, batch sizes, contention optimization)
- Scope-based pre-sorting: use historical data or static analysis to pre-sort
  transactions into likely-non-conflicting batches, reducing re-execution rate

#### Key Files Modified

| File | Change |
|------|--------|
| `controller.cpp` | Parallel execution path in block production |
| `transaction_context.cpp` | Shadow state undo sessions |
| `apply_context.cpp` | Route db ops through shadow state |
| `apply_context.hpp` | Add shadow state member |

#### New Files

| File | Purpose |
|------|---------|
| `block_stm/scheduler.hpp/cpp` | Transaction scheduler |
| `block_stm/multi_version_store.hpp/cpp` | Multi-version data structure |
| `block_stm/shadow_state.hpp/cpp` | Per-transaction state overlay |
| `block_stm/conflict_detector.hpp/cpp` | Read/write set validation |

**No contract changes required.** Shadow state is transparent to WASM execution.
Contracts interact with chain state exclusively through host functions (`db_store_i64`,
`db_find_i64`, etc.). What happens beneath those functions — whether it's a direct
ChainBase write or a shadow state buffer — is invisible to the contract.

**Detailed analysis:**
- [01_chainbase_internals.md](01_chainbase_internals.md) — ChainBase barriers
- [02_transaction_execution_pipeline.md](02_transaction_execution_pipeline.md) — State mutation map
- [03_wasm_threading_model.md](03_wasm_threading_model.md) — VM threading readiness
- [04_block_stm_integration_plan.md](04_block_stm_integration_plan.md) — Full implementation plan

#### Expected Results

| Workload | Speedup | Notes |
|----------|---------|-------|
| Token transfers (different accounts) | 4-8x | Low conflict, high parallelism |
| NFT mints (different collections) | 4-8x | Independent state |
| DEX trades (same market) | 2-4x | Moderate conflict |
| Sequential dependencies | ~1x | Graceful degradation |

---

### Phase 3: Ecosystem & Differentiation (Months 6-18)

These items can be pursued in parallel with Phase 2. Gas model, passkey accounts,
and resource model changes are already in Phase 1.

#### 3A. Migration Tooling (~2-3 months)
**Goal:** Provide existing EOSIO chains a verified, documented upgrade path.

**Snapshot migration:** Chains on Spring/Leap 3.x+ can snapshot and import directly.
Older chains (EOSIO 2.x) must upgrade to Leap first (well-documented existing process).

**Migration contract:** Handles state transformation for each migrating chain:
- Convert old resource model → new model (gas + baseline + deposits)
- Archive Bancor RAM market → fixed-price allocation
- Preserve all accounts, balances, permissions, contract data
- Batched processing to avoid timeout on large chains

**Tools:**
- Snapshot validator (pre-import compatibility check)
- Migration contract (state transformation)
- Migration verifier (pre/post comparison)
- Per-chain migration guide

**Key guarantee:** Any contract that worked on the source chain works on the fork.
Token transfers, staking, voting, multisig — all must match or exceed.

**Detailed plan:** [17_migration_mechanics.md](17_migration_mechanics.md)

#### 3B. Cross-Chain Bridges: Ethereum & Bitcoin
**Goal:** Trustless bridges to ETH and BTC — where the liquidity and users are.

**Key finding:** The cryptographic infrastructure is already in place on both sides.
Savanna uses BLS12-381 — the same curve as Ethereum's beacon chain. Smart contracts
already have access to Keccak-256, secp256k1, BLS pairing, SHA-256, and RIPEMD-160.

**Anvo → Ethereum (trustless):**
- Savanna QCs (~200 bytes) are verifiable on Ethereum via EIP-2537 BLS precompile
- ~360K gas per finality proof verification
- State history plugin already exports all data needed for relay infrastructure

**Ethereum → Anvo (trustless):**
- Beacon chain sync committee BLS signatures verifiable via `bls_pairing` intrinsic
- Merkle-Patricia trie verifier needed as contract code (~500-1000 lines)

**Bitcoin → Anvo (SPV-secure):**
- Double-SHA256, RIPEMD-160, secp256k1 all available as intrinsics
- Standard SPV light client pattern in contract code

**Anvo → Bitcoin (federated):**
- Bitcoin can't verify external proofs natively — requires multisig federation
- Same limitation every BTC bridge faces

**Existing asset: Libre Crosslink** (`/opt/dev/libre-crosslink/`)
- Production-grade multi-chain bridge already handling BTC and ETH/USDT
- Go bridge node with TSS/MPC threshold signing (no single node holds keys)
- 5-node testnet mesh validated, 14 ADRs documenting security architecture
- Bridge node is **chain-agnostic** — porting to Spring fork is config changes only
- Smart contracts (C, EOSIO CDT) need redeployment with updated account names

**Effort WITH Crosslink:** ~3-4 months (port, Savanna finality integration, remaining work)
**Effort WITHOUT Crosslink:** ~11 months (from scratch)
**Savings:** ~7-8 months of engineering time.

**Detailed plans:**
- [09_cross_chain_eth_btc.md](09_cross_chain_eth_btc.md) — Crypto primitive analysis
- [10_libre_crosslink_integration.md](10_libre_crosslink_integration.md) — Crosslink integration plan

#### 3C. Identity & Data Ecosystem (Months 9-18)
**Goal:** Give every person sovereignty over their digital identity and data.

**The Stack:**
```
Applications (social, governance, DeFi, marketplaces)
     ↑ consume identity + data with user permission
Data Ecosystem (vaults, selective disclosure, data marketplace)
     ↑ user-controlled encrypted storage
Identity (personhood, attestations, reputation, social graph)
     ↑ layered proof of personhood
Chain (passkey accounts, permissions, gas model, bridges)
```

**Identity — Layered Proof of Personhood:**
- Layer 0: Account exists (deposit paid) — baseline
- Layer 1: Hardware attestation (passkey device proof) — medium confidence
- Layer 2: Social vouching (web of trust, N verified users vouch) — medium-high
- Layer 3: Biometric uniqueness (ZK proof, no biometric on-chain) — very high
- Layer 4: Credential attestation (KYC, gov ID, degrees — hash anchored) — configurable
- Layer 5: Reputation accrual (account age, activity, community standing) — emergent

Applications declare which layers they require. Users accumulate signals over time.
Permission system gates actions by identity level via existing `linkauth` mechanism.

**Data Ecosystem:**
- User-controlled encrypted data vaults (profile, social, financial, activity)
- Selective disclosure (grant/revoke per-field, per-application)
- Verifiable credentials (W3C VC compatible attestations)
- Data marketplace (users sell access to their data, earn tokens directly)
- Full portability (switch apps without losing identity, data, or reputation)

**What This Enables:**
- One-person-one-vote governance (not stake-weighted)
- Sybil-resistant airdrops and quadratic funding
- UBI distribution to verified unique humans
- Reputation-based DeFi (under-collateralized lending)
- Social platforms without platform risk (you own your data)

**Detailed plan:** [13_identity_data_ecosystem.md](13_identity_data_ecosystem.md)

#### 3D. Integrated Indexer / API Node (~4-5 months)
**Goal:** Build the indexer into the node distribution as a first-class node type,
not a bolted-on external service.

**Concept:** An `index-api` node mode that combines block production/validation with
built-in indexing and a rich query API. Operators run one binary, get full indexing
out of the box.

**Why:**
- EOSIO's biggest operational pain point: running nodeos + Hyperion + Elasticsearch
  as separate services, each with their own config, scaling, and failure modes
- Other chains ship indexing as part of the node (Solana's RPC, Sui's indexer)
- Identity and data ecosystem features need purpose-built queries that generic
  indexers can't provide
- Simplifies developer onboarding: one endpoint for everything

**Architecture:**
- New node plugin: `index_api_plugin` (alongside existing chain_api_plugin)
- Consumes state history internally (no WebSocket hop)
- Stores indexed data in embedded PostgreSQL or SQLite
- Serves rich API: action history, token balances, identity queries, gas analytics
- Optional: can be disabled for pure validator nodes (no indexing overhead)

**Indexed data (MVP):**
- Action history per account
- Token transfer history
- Account lifecycle events (creation, permission changes)
- Gas payment records
- Identity attestations and vouching events
- Contract table change history

**Detailed plan:** [16_indexing_api_layer.md](16_indexing_api_layer.md)

#### 3E. Developer Experience
- Modern SDK (TypeScript/Rust) with passkey + gas + identity support
- Block explorer built on index-api endpoints
- Contract development toolkit with testing framework
- Integration with popular wallets

#### 3F. Sub-Second Finality (Research)
- Savanna's two-chain rule requires 2 QC rounds (~2-3s)
- Aptos achieves ~900ms with pipelined HotStuff from the same lineage
- Investigate: single-chain finality, pipelined QC aggregation
- This would be a protocol-level change — approach with care

---

## Compatibility Guarantees

### What MUST NOT Break (Sacred)
- WASM smart contract execution (existing contracts run unmodified)
- Core chain API endpoints (existing SDKs and tools keep working)
- Account and permission model
- Action and transaction format
- Table access patterns (`db_*_i64` host functions)
- ABI serialization format

### What CAN Evolve
- Consensus parameters (block time, finalizer count, QC thresholds)
- Resource model (billing, limits, staking mechanics)
- System contracts (governance, voting, resource allocation)
- Node operator interfaces (config, monitoring, deployment)
- Header extensions (new metadata without breaking existing parsers)

### Migration Path for Existing Chains
1. Chain takes state snapshot on current EOSIO/Antelope version
2. Snapshot imported into new chain's genesis
3. All accounts, balances, deployed contracts, table data preserved
4. Chain boots with new consensus and runtime
5. Existing contracts execute without recompilation

---

## Team Requirements

**Note:** Libre Crosslink was built in ~5 weeks by a solo developer working with
Claude. These team estimates are for the full scope if expanding beyond that model.

### Core (Months 1-6)
- **1-2 senior C++ developers** — systems programming, blockchain internals
- **1 SDK/tooling developer** — TypeScript SDK, WebAuthn integration
- **1 build/DevOps engineer** — CI/CD, multi-arch builds, packaging

### Expanded (Months 6-12)
- **1-2 additional C++ developers** — Block-STM, indexer plugin
- **1 documentation/community lead** — migration guides, ecosystem outreach

### Key Expertise Needed
- LLVM / compiler internals (for LLVM modernization + AArch64 port)
- Concurrent data structures (for Block-STM)
- EOSIO/Antelope protocol knowledge (for compatibility and migration)
- AArch64 architecture (for ARM port)

### Build Environment
- **Verified:** Ubuntu 24.04, GCC 13.3, CMake 3.28
- **eos-vm-oc:** Requires LLVM 7-11 (not available on Ubuntu 24.04). Build with
  `-DENABLE_OC=OFF` until LLVM compat is patched (Phase 1E).
- **Without OC:** Interpreter + JIT runtimes work. Sufficient for testnet T1.

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Block-STM overhead in low-parallelism workloads | Medium | Low | Feature flag for sequential fallback |
| AArch64 performance regression | Low | Medium | Graviton benchmarking throughout development |
| EOSIO compatibility breaks | Low | High | Comprehensive contract test suite from existing chains |
| Existing EOSIO chains don't adopt | Medium | High | Strong migration tooling, clear value proposition |
| LLVM version compatibility issues | High | High | Spring requires LLVM 7-11; modern Linux ships 14+. Must patch before OC or AArch64 work. Mitigated: build without OC initially, patch LLVM compat in Phase 1E. |
| ChainBase limitations block Block-STM | Medium | Medium | Shadow state avoids modifying ChainBase directly |

---

## Success Metrics

### Technical
- [ ] All existing EOSIO contract test suites pass unmodified
- [ ] 4x+ throughput improvement on realistic workloads (Block-STM)
- [ ] <15% performance gap between x86_64 and AArch64
- [ ] <3 second finality maintained
- [ ] Snapshot migration from EOS mainnet demonstrated

### Ecosystem
- [ ] At least one existing EOSIO chain commits to testing migration
- [ ] SDK available in TypeScript and Rust
- [ ] Block explorer operational
- [ ] Documentation covers migration, development, and node operation

---

## Research Documents Index

| Doc | Topic |
|-----|-------|
| [01](01_chainbase_internals.md) | ChainBase memory model, undo stack, concurrency barriers |
| [02](02_transaction_execution_pipeline.md) | Transaction execution flow, state mutations, dependency graph |
| [03](03_wasm_threading_model.md) | WASM runtime threading, per-thread isolation, host functions |
| [04](04_block_stm_integration_plan.md) | Block-STM architecture, shadow state, scheduler design |
| [05](05_architecture_portability.md) | x86_64 dependency inventory, porting strategies |
| [06](06_aarch64_port_implementation.md) | AArch64 port file-by-file implementation plan |
| [07](07_rebrand_plan.md) | Rebrand inventory (~11K refs), execution plan, protocol-safety boundaries |
| [08](08_system_account_compatibility.md) | Genesis-configurable system account names, compatibility switch design |
| [09](09_cross_chain_eth_btc.md) | Ethereum & Bitcoin bridge design, crypto primitive inventory, relay architecture |
| [10](10_libre_crosslink_integration.md) | Libre Crosslink integration — existing BTC/ETH bridge, porting plan, 7-8mo savings |
| [11](11_resource_model_evolution.md) | Baseline + Staking + Parallel Gas resource model, RAM market removal, legacy impact |
| [12](12_passkey_accounts.md) | Passkey-first accounts, WebAuthn infrastructure, 5-second onboarding, social recovery |
| [13](13_identity_data_ecosystem.md) | Identity + data platform: layered personhood, data vaults, selective disclosure, marketplace |
| [14](14_system_contracts.md) | System contract starting point, required modifications, boot sequence |
| [15](15_testnet_strategy.md) | Phased testnet strategy: internal → features → infra → public → stress |
| [16](16_indexing_api_layer.md) | Integrated indexer as node plugin, API layer, block explorer |
| [17](17_migration_mechanics.md) | EOSIO chain migration: snapshot format, state transformation, migration contracts |
| [18](18_licensing.md) | BSL 1.1 license: terms, Additional Use Grant, change date, contributor DCO, messaging |
