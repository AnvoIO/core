# Research: Anvo Network — Technical Analysis

**[FORK_PLAN.md](FORK_PLAN.md)** — The master plan. Start here.

This directory contains deep technical analysis of the Spring (AntelopeIO) codebase,
focused on building Anvo Network with parallel transaction execution, multi-architecture
support, passkey accounts, and a sovereign identity platform.

## Documents

### Parallel Execution

1. **[01_chainbase_internals.md](01_chainbase_internals.md)** — Deep dive into ChainBase,
   the memory-mapped state database. Covers memory model, undo/session system, index
   architecture, and specific barriers to concurrent writes.

2. **[02_transaction_execution_pipeline.md](02_transaction_execution_pipeline.md)** — Complete
   trace of transaction execution from receipt to state commitment. Enumerates every shared
   state mutation and maps the dependency graph between transactions.

3. **[03_wasm_threading_model.md](03_wasm_threading_model.md)** — Analysis of the WASM
   runtime's threading model. Documents per-thread isolation (executor, memory, base register),
   host function categorization, and code cache thread safety.

4. **[04_block_stm_integration_plan.md](04_block_stm_integration_plan.md)** — Concrete
   implementation plan for Block-STM parallel execution. Covers multi-version data
   structures, shadow state layer, scheduler design, and integration with the controller.

### Architecture Portability

5. **[05_architecture_portability.md](05_architecture_portability.md)** — Complete inventory
   of x86_64 dependencies. Analysis of porting strategies (port OC runtime vs. replace with
   Wasmtime/Cranelift). Covers AArch64, Apple Silicon, and RISC-V considerations. **RESOLVED.**

6. **[06_aarch64_port_implementation.md](06_aarch64_port_implementation.md)** — File-by-file
   implementation plan for porting Core VM OC to AArch64. Covers GS→X28 register replacement,
   LLVM IR changes, signal handler updates, memory layout, and bugs found. **COMPLETE.**

### Rebrand

7. **[07_rebrand_plan.md](07_rebrand_plan.md)** — Complete inventory of ~11,000+ branding
   references (eosio, antelope, spring, leap, nodeos, cleos, keosd). Phased execution plan
   with protocol-safety boundaries. ~1-2 week effort.

8. **[08_system_account_compatibility.md](08_system_account_compatibility.md)** — Design for
   genesis-configurable system account names. Migrating chains keep `eosio.*`, new chains
   use custom names. ~1-2 week effort.

### Cross-Chain

9. **[09_cross_chain_eth_btc.md](09_cross_chain_eth_btc.md)** — Ethereum and Bitcoin
   bridge design. BLS12-381 proof compatibility with EIP-2537, crypto primitive inventory,
   relay architecture, SPV light client design.

10. **[10_libre_crosslink_integration.md](10_libre_crosslink_integration.md)** — Libre Crosslink
    is an existing production-grade BTC/ETH bridge with TSS/MPC signing. Chain-agnostic
    bridge node ports via config changes. Saves ~7-8 months vs building from scratch.

### Resource Model

11. **[11_resource_model_evolution.md](11_resource_model_evolution.md)** — Baseline + Staking +
    Parallel Gas model. Two paths to resources (free via staking OR pay gas), free baseline
    per account, RAM market removal. Zero breaking changes for legacy chains. ~2-3 months.

### Account Model

12. **[12_passkey_accounts.md](12_passkey_accounts.md)** — Passkey-first accounts using existing
    WebAuthn support. 5-second onboarding, multi-device management, social recovery. No seed
    phrases. Protocol-level — not a smart contract wrapper. ~2-3 months.

### Identity & Data

13. **[13_identity_data_ecosystem.md](13_identity_data_ecosystem.md)** — Platform thesis:
    sovereign identity and user-controlled data. Layered proof of personhood (hardware,
    social, ZK biometric, credentials, reputation). Data vaults with selective disclosure.
    Data marketplace. Enables sybil-resistant governance, fair distributions, reputation
    lending, portable social platforms.

### Foundation Infrastructure

14. **[14_system_contracts.md](14_system_contracts.md)** — System contract starting point
    (Spring bundled contracts), required modifications for gas/deposits/baseline/RAM,
    new contract repo structure, boot sequence.

15. **[15_testnet_strategy.md](15_testnet_strategy.md)** — Five-phase testnet: internal
    (rebranded boot) → features (gas + passkeys) → infra (ARM + bridge) → public
    (SDK + docs) → stress (Block-STM). Mainnet readiness criteria.

16. **[16_indexing_api_layer.md](16_indexing_api_layer.md)** — Integrated indexer as
    first-class node plugin (`index_api_plugin`). Built-in action history, token queries,
    identity/gas analytics. One binary, no external Elasticsearch/Hyperion dependency.

17. **[17_migration_mechanics.md](17_migration_mechanics.md)** — EOSIO chain upgrade path:
    snapshot compatibility matrix, migration contracts for state transformation, RAM market
    conversion, per-chain migration guides, testing strategy.

### Licensing

18. **[18_licensing.md](18_licensing.md)** — BSL 1.1 with 3-year conversion to Apache 2.0.
    Additional Use Grant permits all ecosystem use, restricts competing L1 forks. Full
    license text, contributor DCO, community messaging guidance, Crosslink licensing options.

### Governance

21. **[21_governance_dao.md](21_governance_dao.md)** — Governance/DAO contract design.
    Libre btc-libre-governance analysis, other DAO models (Aragon, Compound, Cosmos),
    proposal lifecycle, paginated vote counting, delegation, identity-gated participation.

### Ecosystem Analysis

22. **[22_antelope_chain_comparison.md](22_antelope_chain_comparison.md)** — System contract
    comparison across WAX, Telos, UX Network, and Libre. Feature-by-feature analysis of
    producer management, voting, pay models, resource models, governance, and identity.
    Recommendations for what to adopt, adapt, or skip.

### Implementation Details

19. **[19_genesis_accounts_implementation.md](19_genesis_accounts_implementation.md)** —
    Genesis-configurable system accounts implementation: config struct, global accessors,
    432+ call site conversions, snapshot v9, test suite.

20. **[20_core_vm_oc_architecture.md](20_core_vm_oc_architecture.md)** — Core VM OC
    architecture reference: code cache format, compilation pipeline, AArch64 register
    strategy, ADRP alignment, signal handling, migration guide. **Current reference doc.**

## Key Findings

### Parallel Execution
- **ChainBase** is the primary bottleneck — single-writer, single undo stack, no transaction isolation
- **WASM runtime** is mostly ready — per-thread executors, memory, and allocators already exist
- **Global sequence counters** create 100% conflict rate but can be deferred to a post-execution pass
- **Block-STM** is the best-fit approach — no protocol changes, deterministic, graceful degradation
- Most real-world EOSIO transaction workloads are low-conflict → 4-8x speedup expected

### Architecture Portability — COMPLETE
- ~80% of x86_64 coupling was in the OC runtime (GS segment, stack switching asm, LLVM codegen)
- The **interpreter already runs on any architecture** — ARM support is trivially available at reduced performance
- **Core VM OC ported to AArch64** using dedicated X28 register (doc 06) — all tests pass
- 7 bugs found and fixed during port (3 latent ORCv2 bugs, 4 AArch64-specific)
- Both x86_64 and AArch64 verified, v0.0.1-alpha packages built for both architectures
