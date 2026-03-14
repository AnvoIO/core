# Cross-Chain Messaging: Ethereum & Bitcoin Bridges

## Key Finding

Spring's cryptographic infrastructure is **remarkably well-suited** for trustless
bridges to both Ethereum and Bitcoin. The primitives are already in place on both
sides — this is primarily an integration and smart contract engineering effort,
not a cryptographic research problem.

> **Note:** This document analyzes the crypto primitives and from-scratch effort
> estimates. After this analysis was written, the existing Libre Crosslink bridge
> was identified as a major head start. See [10_libre_crosslink_integration.md](10_libre_crosslink_integration.md)
> for the integration plan, which reduces bridge effort from ~11 months to ~3-4 months.

## Part 1: Anvo → Ethereum (Proving Finality on ETH)

### Why This Works

Savanna's BLS12-381 finality proofs are **directly verifiable on Ethereum** via the
EIP-2537 BLS precompile. Same curve, same math, standard serialization.

### What a Savanna QC Contains

```
qc_t {
    block_num:          uint32_t        (4 bytes)
    active_policy_sig: {
        strong_votes:   bitset          (~1-4 bytes for typical finalizer sets)
        weak_votes:     bitset          (~1-4 bytes, optional)
        sig:            bls_agg_sig     (192 bytes — one signature regardless of signer count)
    }
    pending_policy_sig: optional<...>   (same structure, for policy transitions)
}
```

**Total QC size: ~200 bytes** for a typical strong QC. This is tiny — BLS aggregation
compresses N finalizer signatures into a single 192-byte G2 point.

### Verification on Ethereum (Solidity)

An Ethereum smart contract can verify a Savanna finality proof using EIP-2537:

```
1. Decode QC from block extension
2. Load finalizer public keys (96 bytes each × N, stored on-chain once)
3. Aggregate pubkeys according to vote bitset
4. Call EIP-2537 BLS AGGREGATE_VERIFY precompile (0x0a):
   AGGREGATE_VERIFY(aggregated_pubkeys, finality_digest, aggregate_signature)
5. Verify action_mroot against the signed finality digest
6. Verify Merkle proof for specific action/transaction
```

**Estimated gas cost:** ~360K gas per finality proof verification (~$0.05-0.20)

### Proof Chain: Transaction → Finality

Spring provides a complete cryptographic commitment chain:

```
Transaction
  → Action Digest (SHA256)
    → Action Merkle Root (binary Merkle tree, SHA256)
      → Block Header (action_mroot field)
        → Finality Leaf Node (block_num, timestamp, finality_digest, action_mroot)
          → Validation Tree Root (incremental Merkle tree)
            → Finality Digest (signed by finalizers)
              → QC Signature (BLS12-381 aggregate)
```

An external verifier can prove: **"Action X was executed and finalized in block B,
attested by finalizers F1..Fn with aggregate BLS signature S"**

Merkle proof size: ~32 + 32×log2(N) bytes. For 1000 actions: ~320 bytes.

### Relay Architecture

```
Spring Chain                    Ethereum
     |                              |
     | (blocks + QCs)               |
     v                              |
State History Plugin                |
(WebSocket, fetch_finality_data)    |
     |                              |
     v                              |
Relay Node                          |
- Subscribe to finality data        |
- Extract QC signatures             |
- Build Merkle proofs               |
- Pack for EIP-2537                 |
     |                              |
     +------ submit proof --------->|
                                    v
                          Solidity Light Client
                          - Verify BLS signature
                          - Check Merkle proof
                          - Update finalized state
                          - Execute bridge logic
```

The `state_history_plugin` already exports everything needed via WebSocket:
- Full signed blocks
- Finality data (`action_mroot`, QC claims, finalizer policies)
- Action traces for Merkle proof construction

### What Needs to Be Built

| Component | Language | Effort |
|-----------|----------|--------|
| Solidity light client contract | Solidity | 4-6 weeks |
| Relay node (listener + proof builder) | TypeScript/Rust | 4-6 weeks |
| Finalizer policy tracking on-chain | Solidity | 1-2 weeks |
| Integration tests | Both | 2-3 weeks |
| **Total** | | **~3-4 months** |

---

## Part 2: Ethereum → Anvo (Verifying ETH on-chain)

### Available Smart Contract Intrinsics

Spring already exposes everything needed to verify Ethereum data inside smart contracts:

| Intrinsic | Purpose | Ethereum Use |
|-----------|---------|-------------|
| `sha3(data, hash, keccak=1)` | Keccak-256 hash | Ethereum's native hash function |
| `k1_recover(sig, digest, pub)` | secp256k1 ECDSA recovery | Verify Ethereum EOA signatures |
| `bls_g1_add`, `bls_g2_add`, `bls_pairing` | BLS12-381 operations | Verify Ethereum beacon chain finality |
| `alt_bn128_add/mul/pair` | BN128 curve operations | Verify ZK proofs from Ethereum |
| `sha256(data, hash)` | SHA-256 | General Merkle proofs |
| `blake2_f(...)` | BLAKE2b compression | EIP-152 compatibility |
| `mod_exp(base, exp, mod, out)` | Modular exponentiation | RSA, group operations |

### Ethereum Beacon Chain Light Client (on Anvo)

Post-merge Ethereum uses **Beacon Chain consensus** with BLS12-381 signatures —
the same curve Anvo uses for Savanna finality. A smart contract on Anvo
can verify Ethereum beacon chain sync committee signatures:

```
1. Receive Ethereum beacon block header + sync committee signature
2. Use bls_pairing intrinsic to verify BLS12-381 aggregate signature
3. Verify state root from beacon block header
4. Verify Merkle proof of specific storage slot or receipt against state root
5. Confirm: "Ethereum transaction X was finalized at slot S"
```

This is a **fully trustless** Ethereum light client running as a smart contract.

### Ethereum Transaction Verification

To prove an Ethereum transaction/event occurred:

```
1. Verify beacon chain finality (BLS signature via bls_pairing)
2. Extract execution layer state root from beacon block
3. Verify receipt Merkle-Patricia proof against receipts root
4. Extract event logs from receipt
5. Process bridge action based on verified event
```

**Missing piece:** Merkle-Patricia trie verification is not a native intrinsic.
It needs to be implemented in contract code using Keccak-256 and RLP decoding.
This is ~500-1000 lines of contract code — well-understood, many open-source
implementations exist.

### What Needs to Be Built

| Component | Where | Effort |
|-----------|-------|--------|
| Ethereum beacon light client contract | Anvo (C++ WASM) | 6-8 weeks |
| Merkle-Patricia trie verifier | Anvo (C++ WASM) | 2-3 weeks |
| RLP decoder library | Anvo (C++ WASM) | 1-2 weeks |
| Sync committee rotation tracking | Anvo (C++ WASM) | 2-3 weeks |
| Relay node (ETH → Anvo) | TypeScript/Rust | 3-4 weeks |
| Integration tests | Both | 2-3 weeks |
| **Total** | | **~4-6 months** |

---

## Part 3: Bitcoin → Anvo (Verifying BTC on-chain)

### Available Primitives for Bitcoin

| Intrinsic | Bitcoin Use |
|-----------|------------|
| `sha256` (chain twice) | Double-SHA256 (Bitcoin block/tx hash) |
| `ripemd160` | Bitcoin address generation (HASH160 = RIPEMD160(SHA256(pubkey))) |
| `k1_recover` / `assert_recover_key` | secp256k1 ECDSA (Bitcoin signatures) |

### Bitcoin SPV Verification

A smart contract on Anvo can verify Bitcoin SPV (Simplified Payment Verification)
proofs:

```
1. Receive Bitcoin block header (80 bytes)
2. Verify proof-of-work: double_sha256(header) < target
3. Track block headers to verify chain of work (longest chain rule)
4. Receive Merkle proof for specific transaction
5. Verify transaction is included in block via Merkle proof
6. Confirm: "Bitcoin transaction X was included in block B at height H"
```

### Bitcoin SPV Light Client

```
Contract stores:
- Latest known Bitcoin block header hash
- Accumulated chain work
- Difficulty adjustment parameters

Relay submits:
- New block headers (80 bytes each)
- Transaction + Merkle proof for cross-chain transfers

Contract verifies:
- Header chain validity (prev_hash links)
- Proof of work (double-SHA256 < difficulty target)
- Sufficient confirmations (e.g., 6 blocks)
- Transaction inclusion via Merkle proof
```

**This is a well-understood pattern.** tBTC, RenBTC, and other projects have implemented
Bitcoin SPV verification on Ethereum. The same approach works here with the available
SHA256 + RIPEMD160 + secp256k1 primitives.

### Bitcoin Finality Model

Bitcoin has **probabilistic finality** — you wait for N confirmations (typically 6)
and assume the work required to rewrite the chain is prohibitively expensive.

Your bridge contract tracks:
- Block headers with cumulative work
- Requires N confirmations before accepting a proof
- Handles reorgs by tracking multiple chain tips

### What Needs to Be Built

| Component | Where | Effort |
|-----------|-------|--------|
| Bitcoin SPV light client contract | Anvo (C++ WASM) | 4-6 weeks |
| Bitcoin header chain tracker | Anvo (C++ WASM) | 2-3 weeks |
| Bitcoin transaction parser | Anvo (C++ WASM) | 2-3 weeks |
| Bitcoin relay node | TypeScript/Rust | 3-4 weeks |
| Integration tests | Both | 2-3 weeks |
| **Total** | | **~3-5 months** |

---

## Part 4: Bridge Token Architecture

### Wrapped Assets Pattern

```
ETH → Anvo:
1. User locks ETH in Ethereum bridge contract
2. Relay submits proof of lock event to Anvo
3. Anvo's bridge contract verifies the proof
4. Mints wrapped ETH (wETH) on Anvo
5. User has wETH, backed 1:1 by locked ETH

Anvo → ETH:
1. User burns wETH on Anvo
2. Relay submits finality proof of burn action to Ethereum
3. Ethereum bridge contract verifies QC + Merkle proof
4. Releases locked ETH to user

BTC → Anvo:
1. User sends BTC to a designated bridge address
2. Relay submits SPV proof of Bitcoin transaction
3. Anvo verifies proof-of-work + Merkle inclusion
4. Mints wrapped BTC (wBTC) on Anvo
5. User has wBTC, backed by locked BTC
```

### Trust Model

| Direction | Trust Model | Why |
|-----------|------------|-----|
| Anvo → ETH | **Trustless** | BLS finality proofs verified on-chain via EIP-2537 |
| ETH → Anvo | **Trustless** | Beacon chain BLS proofs verified on-chain via bls_pairing |
| BTC → Anvo | **SPV-secure** | Proof-of-work verified on-chain, N confirmations |
| Anvo → BTC | **Federated** | Bitcoin can't verify external proofs natively |

**The BTC → Anvo direction is trustless (SPV-secure).**
**The Anvo → BTC direction requires a federation** (multisig or similar) because
Bitcoin Script can't verify BLS signatures or Merkle proofs from external chains.
This is the same limitation every BTC bridge faces.

---

## Implementation Roadmap

### Phase 1: Ethereum Bridge (Months 8-12)

**Month 8-9: Anvo → Ethereum**
- Build Solidity light client for Savanna QC verification
- Build relay node subscribing to state_history_plugin
- Test: prove finalized actions on Ethereum testnet

**Month 9-11: Ethereum → Anvo**
- Build beacon chain light client as WASM contract
- Implement Merkle-Patricia verifier + RLP decoder
- Build ETH → Anvo relay node
- Test: prove Ethereum events on Anvo testnet

**Month 11-12: Token Bridge**
- Deploy lock/mint contracts on both sides
- End-to-end wrapped asset flow
- Security audit

### Phase 2: Bitcoin Bridge (Months 10-14)

**Month 10-12: BTC → Anvo**
- Build Bitcoin SPV light client as WASM contract
- Build BTC block header relay
- Test: verify Bitcoin transactions on Anvo

**Month 12-14: Anvo → BTC (Federated)**
- Design federation model (threshold multisig)
- Build federation signing infrastructure
- Deploy BTC release mechanism

### Phase 3: Production Hardening (Months 14-16)

- Security audits (both Solidity and WASM contracts)
- Economic security analysis
- Rate limiting, circuit breakers
- Monitoring and alerting infrastructure

---

## What Makes This Feasible

1. **BLS12-381 on both sides** — Savanna and Ethereum Beacon Chain use the same curve.
   Anvo can verify ETH. ETH can verify Anvo. Same math, no translation layer.

2. **Crypto intrinsics already in place** — Keccak-256, secp256k1, BLS12-381 pairing,
   alt_bn128, SHA-256, RIPEMD-160 are all exposed to smart contracts. No protocol
   upgrades needed.

3. **State history plugin** — already exports finality data in a format suitable for
   relay infrastructure. No new node software needed.

4. **Compact proofs** — QCs are ~200 bytes. Merkle proofs are ~320 bytes. Total proof
   submission is under 1KB. Cheap to verify on Ethereum (~360K gas).

5. **Well-understood patterns** — BTC SPV light clients and ETH beacon light clients
   have been implemented multiple times across the industry. This isn't novel research.
