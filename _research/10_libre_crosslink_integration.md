# Libre Crosslink Integration — Existing Bridge Infrastructure

## Key Finding

Libre Crosslink is a **mature, production-grade cross-chain bridge** that already handles
both Bitcoin and Ethereum. It's testnet-validated with a 5-node mesh, and the bridge node
code is chain-agnostic. Porting to the Spring fork is a configuration change, not a rewrite.

**This eliminates ~7-8 months of bridge development from the fork plan.**

## What Crosslink Already Provides

### Architecture

```
Bitcoin Network          Libre/Spring Chain         Ethereum Network
      |                        |                         |
      |                   Bridge Node (Go)                |
      |              (Noise_IK P2P mesh, N nodes)         |
      |                   /    |    \                     |
      |        Orchestrator  Signers  Poller              |
      |              |         |        |                 |
      +-- BTC RPC ---+    TSS/MPC   Libre API ---+-- ETH RPC --+
                      |   Ceremony     |
                      v                v
                x.libre contract   t.libre contract
                (BTC bridge)       (USDT bridge)
```

### Components

| Component | Status | Technology |
|-----------|--------|-----------|
| Bridge node (relay + orchestrator) | Working (testnet) | Go 1.24 |
| BTC peg-in (BTC → chain) | Smoke-tested | btcsuite + SPV in contract |
| BTC peg-out (chain → BTC) | Smoke-tested | TSS/MPC ECDSA signing |
| ETH/USDT peg-in (ETH → chain) | Smoke-tested | go-ethereum + ChainMonitor |
| ETH/USDT peg-out (chain → ETH) | Smoke-tested | TSS/MPC ECDSA + EIP-1559 |
| Libre smart contracts (BTC) | Deployed (testnet) | C (EOSIO CDT 4.0) |
| Libre smart contracts (USDT) | Deployed (testnet) | C (EOSIO CDT 4.0) |
| Ethereum smart contracts | NOT STARTED | Solidity (placeholder) |
| Node registry (bridgereg) | Working | C (EOSIO CDT 4.0) |
| P2P networking | Working | Noise_IK encrypted |
| TSS key generation | Working | tss-lib fork |
| TSS key resharing (rotation) | Working | tss-lib fork |
| Monitoring | Working | Prometheus + Grafana |
| Testing framework | Working | TypeScript/Jest |
| CI/CD | Working | GitLab CI |

### Trust Model

**Multi-party threshold security**, not a simple federation:

- **Orchestrators** build transactions and coordinate TSS ceremonies
- **Signers** independently verify all work against on-chain contract state
- **Contracts** are the source of truth for thresholds, node lists, authorization
- **TSS/MPC** ensures no single node ever holds a complete signing key
- **Three-sided verification** (ADR 0010): every action verified by orchestrator,
  signers, AND contract independently
- **Four-gate crash recovery** (ADR 0008): persistent state machine prevents
  double-spends and lost transactions on crash/restart

### Security Design (ADR-Documented)

14 Architecture Decision Records covering:
- TSS security hardening with ZK proof enforcement (ADR 0007)
- Contract safety controls with pause/freeze flags (ADR 0012)
- Config authority: contract-authoritative, fail-closed startup (ADR 0015)
- BTC UTXO selection and consolidation strategy (ADR 0011)
- P2P QoS, backpressure, quorum gating (ADR 0014)
- Work dispatch with priority queues and TSS serialization (ADR 0017)

## Porting to the Spring Fork

### What Changes

| Item | Change | Effort |
|------|--------|--------|
| Libre API endpoint | Config: `libre.apiURL` | Minutes |
| Registry contract account | Config: `bridgeNode.registryContract` | Minutes |
| Bridge node account | Config: `bridgeNode.libreAccount` | Minutes |
| Contract deployment | Redeploy bridgereg, x.libre, t.libre to new chain | Hours |
| Account names | Update contract account names if rebranded | Hours |
| Token symbols | Verify BTC/USDT symbol conventions match | Hours |
| Chain finality timing | Adjust confirmation counts for Savanna | Hours |

### What Doesn't Change

- Bridge node Go code — chain-agnostic, uses `eoscanada/eos-go` which works with any EOSIO/Antelope API
- TSS ceremony code — no chain dependency
- P2P networking — no chain dependency
- Bitcoin integration — no chain dependency
- Ethereum integration — no chain dependency
- Monitoring infrastructure — no chain dependency

### Savanna-Specific Opportunity

Crosslink currently uses simple block confirmation counts for finality. With Savanna's
explicit finality (QCs), the bridge can be upgraded to use **true finality** instead
of probabilistic confirmation counting:

```
// Current: wait N blocks
if currentBlock - txBlock >= requiredConfirmations { finalized = true }

// Improved: check Savanna finality
if txBlock <= lastIrreversibleBlock { finalized = true }
```

This is a small code change in the poller that gives the bridge **faster and
cryptographically guaranteed finality** — a meaningful improvement over the
current confirmation-counting approach.

## Remaining Work (Crosslink-Side)

### Critical (Before Production)

| Item | Status | Notes |
|------|--------|-------|
| Chain reorg detection | BACKLOG (CRITICAL) | ETH reorg handling needed |
| Production config audit | PENDING | Mainnet-specific values |
| Security audit | PENDING | External review of TSS + contracts |
| Legacy key share import | BACKLOG | Migration from legacy bridge |

### Planned Features

| Item | Status | Notes |
|------|--------|-------|
| Ethereum smart contracts (deposit wallets) | NOT STARTED | Currently using CREATE2 derivation without deployment |
| Multi-ERC20 support | BACKLOG | Extend beyond USDT to other tokens |
| I2P transport | FUTURE | Location hiding for bridge nodes |
| Contract upgradability | FUTURE | core.msig gating + time-lock |

## Impact on Fork Plan

### Before (Without Crosslink)

Phase 3B estimated:
- ETH bridge: ~6 months
- BTC bridge: ~5 months
- Total: ~11 months of bridge development

### After (With Crosslink)

Phase 3B reduces to:
- Port Crosslink to new chain: ~1-2 weeks
- Savanna finality integration: ~1-2 weeks
- Complete remaining Crosslink work (ETH contracts, reorg detection): ~2-3 months
- Production hardening + security audit: ~1-2 months
- **Total: ~3-4 months** (vs. 11 months from scratch)

**Savings: ~7-8 months of engineering time.**

### Integration Strategy

1. **Fork Crosslink** alongside the Spring fork
2. **Update contract account names** to match the new chain's naming
3. **Redeploy contracts** to the new chain's testnet
4. **Update bridge node config** to point to new chain API
5. **Integrate Savanna finality** into the poller for faster bridge confirmations
6. **Complete remaining Crosslink work** (ETH contracts, reorg detection)
7. **Security audit** (covers both bridge and chain)
