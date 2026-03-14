# EOSIO Chain Migration Guide

## Overview

Existing EOSIO chains (EOS, Telos, WAX, UX Network, etc.) can migrate to Anvo Network
while preserving all accounts, contracts, and on-chain state. No recompilation of smart
contracts is required.

## What Is Preserved

- All accounts and permissions
- All deployed smart contracts (WASM bytecode)
- All contract table data
- Token balances
- System account names (`eosio`, `eosio.token`, `eosio.msig`, etc.)

## What Changes

| Component | Before (EOSIO/Leap/Spring) | After (Anvo Network) |
|---|---|---|
| Node daemon | `nodeos` | `core_netd` |
| CLI wallet tool | `cleos` | `core-cli` |
| Key manager | `keosd` | `core-wallet` |
| Plugin namespace | `eosio::` | `core_net::` |
| Config directory | `etc/eosio/` | `etc/core_net/` |
| Snapshot format | v2-v8 | v9 (reads v2-v8) |

System account names remain `eosio.*` on migrated chains. Only new chains use the `core.*`
prefix (see [Genesis Configuration Guide](genesis_configuration.md)).

## Migration Steps

### 1. Take a snapshot on the existing chain

```bash
# On the running nodeos / Spring node:
curl -X POST http://127.0.0.1:8888/v1/producer/create_snapshot
```

Save the resulting snapshot file (e.g., `snapshot-0000000abcdef.bin`).

### 2. Install Anvo Network

Build or install the `core_netd`, `core-cli`, and `core-wallet` binaries.

### 3. Start core_netd from the snapshot

```bash
core_netd \
  --snapshot /path/to/snapshot-0000000abcdef.bin \
  --data-dir /var/lib/core_net/data \
  --config-dir /etc/core_net/ \
  --plugin core_net::producer_plugin \
  --plugin core_net::chain_api_plugin \
  --plugin core_net::http_plugin
```

### 4. Verify

The node reads the snapshot and defaults `system_account_prefix` to `"eosio"`. The chain
continues with all `eosio.*` accounts intact. All existing contracts execute without
modification.

```bash
core-cli get info
core-cli get account eosio
```

## WASM Compatibility

- All existing contracts work **unmodified** — no recompilation needed.
- WASM intrinsics are dual-registered (e.g., both `eosio_assert` and `core_net_assert`
  resolve to the same host function).
- ABI files with `"eosio::abi/1.x"` version strings are accepted.
- The WASM execution engine (eos-vm) is unchanged.

## Snapshot Compatibility

| Snapshot Version | Source | Supported |
|---|---|---|
| v2-v5 | EOSIO 1.8 – 2.x | Yes (auto-defaults to `"eosio"` prefix) |
| v6 | Leap 3.x | Yes (auto-defaults to `"eosio"` prefix) |
| v7-v8 | Spring 1.x | Yes (auto-defaults to `"eosio"` prefix) |
| v9 | Anvo Network | Yes (includes explicit `system_account_prefix`) |

Anvo Network reads snapshots v2 through v9. Older snapshots that lack a
`system_account_prefix` field automatically default to `"eosio"`, ensuring seamless
migration without any manual prefix configuration.
