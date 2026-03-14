# Genesis Configuration Guide

## Overview

The `genesis.json` file defines the initial state of a new blockchain. It is read once
when a node boots for the first time and becomes permanent chain state. After first boot,
the genesis file is no longer consulted — all configuration is stored on-chain.

## Fields

| Field | Description |
|---|---|
| `initial_timestamp` | Chain start time in ISO 8601 format (e.g., `2024-01-01T00:00:00.000`). |
| `initial_key` | Public key for the initial system account. Used to bootstrap the chain before block producers are registered. |
| `initial_configuration` | Chain parameters: block size limits, CPU/NET limits, transaction lifetimes, etc. Defaults are sane for most networks. |
| `system_account_prefix` | *(Optional)* Configures the name prefix for all system accounts. See below. |

## System Account Prefix

The `system_account_prefix` field controls which account names are created as privileged
system accounts at chain genesis.

**If set to `"core"`** (recommended for new Anvo Network chains):
- Creates system accounts: `core`, `core.null`, `core.prods`, `core.auth`, `core.code`, etc.
- The `core.` prefix is reserved for privileged accounts only.

**If omitted or set to `"eosio"`** (for migrating existing EOSIO chains):
- Creates standard EOSIO accounts: `eosio`, `eosio.null`, `eosio.prods`, etc.
- Use this when migrating EOS, Telos, WAX, or other EOSIO-based chains.

**Important behavior:**
- The prefix is stored in the `global_property_object` and persists across restarts and snapshots.
- It **cannot** be changed after the chain boots.
- Snapshots v9+ carry the prefix in the GPO section. Snapshots v2-v8 default to `"eosio"`.

## Example: New Anvo Network Chain

```json
{
  "initial_timestamp": "2024-01-01T00:00:00.000",
  "initial_key": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV",
  "system_account_prefix": "core",
  "initial_configuration": {
    "max_block_net_usage": 1048576,
    "max_block_cpu_usage": 200000,
    "max_transaction_net_usage": 524288,
    "max_transaction_cpu_usage": 150000,
    "max_transaction_lifetime": 3600,
    "max_authority_depth": 6
  }
}
```

## Example: Migrating an EOSIO Chain

```json
{
  "initial_timestamp": "2024-01-01T00:00:00.000",
  "initial_key": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV",
  "initial_configuration": {
    "max_block_net_usage": 1048576,
    "max_block_cpu_usage": 200000,
    "max_transaction_net_usage": 524288,
    "max_transaction_cpu_usage": 150000,
    "max_transaction_lifetime": 3600,
    "max_authority_depth": 6
  }
}
```

No `system_account_prefix` field — defaults to `"eosio"`, preserving existing account names.

## How It Works Internally

1. **First boot from genesis:** The controller reads `genesis.json`, resolves the prefix
   (`system_account_prefix` or default `"eosio"`), and calls
   `config::set_system_accounts(system_accounts::from_prefix(prefix))`.
2. **Stored on-chain:** The prefix is written to the `global_property_object` in chainbase.
3. **Subsequent restarts:** The node reads the prefix from the GPO, not from genesis.
4. **Snapshot boot:** The node reads the prefix from the GPO section of the snapshot.
   Snapshots v2-v8 (from Spring/Leap/EOSIO) auto-default to `"eosio"`. Snapshots v9+
   include the `system_account_prefix` field explicitly.
