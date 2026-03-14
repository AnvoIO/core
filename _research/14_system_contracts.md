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

These actions are handled natively by the node and bypass contract code entirely:

```
eosio::newaccount    — account creation
eosio::setcode       — deploy/update contract code
eosio::setabi        — deploy/update contract ABI
eosio::updateauth    — modify account permissions
eosio::deleteauth    — remove custom permissions
eosio::linkauth      — link action to custom permission
eosio::unlinkauth    — unlink action from custom permission
eosio::canceldelay   — cancel delayed transaction
```

These are registered in `controller.cpp` via `SET_APP_HANDLER` macro. The system
account name is the only thing that needs to change here (doc 08).

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
| Gas pricing tables | `gas_params` table: cpu_price, net_price, ram_price | eosio.system |
| Gas price governance | `setgasprices` action (producer vote) | eosio.system |
| Baseline allocation | On `newaccount`: set baseline cpu/net/ram weights | eosio.system |
| Refundable deposit | `deposit_table`, maturity tracking, `reclaimdeposit` action | eosio.system |
| Deposit sponsor tracking | Who paid deposit, who gets refund | eosio.system |
| Gas fee collection | Collect fees, distribute (burn/treasury/validators) | eosio.system |
| RAM market removal | Remove `buyram`/`sellram` Bancor logic, replace with fixed-price | eosio.system |
| RAM fixed pricing | Governance-set RAM price via `setramprices` | eosio.system |

### From Passkey Accounts (Doc 12)

| Change | What | In Contract |
|---|---|---|
| Hash-based account names | Derive name from pubkey hash on creation | eosio.system |
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
| System account names | Genesis-configurable (eosio.* or core.*) | eosio.system + node |

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
1. Start cored with genesis.json (creates system account + initial key)
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

## Effort

| Component | Effort |
|---|---|
| Fork and rebrand system contracts | 1 week (part of doc 07 rebrand) |
| Gas pricing + collection logic | 1-2 weeks |
| Baseline allocation on account creation | 1 week |
| Refundable deposit system | 3-4 weeks |
| RAM market replacement | 1-2 weeks |
| Identity contracts (new) | 3-4 months (doc 13, Phase 2) |
| Boot sequence scripting | 1 week |
| Testing (all contract changes) | 2-3 weeks |
| **Total (launch-critical)** | **~2-3 months** |
| **Total (with identity)** | **~5-7 months** |
