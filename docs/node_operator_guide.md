# Node Operator Guide

Quick reference for operators transitioning from Spring/EOSIO to Anvo Core.

## Executable Names

| Old | New | Description |
|-----|-----|-------------|
| nodeos | core_netd | Blockchain node daemon |
| cleos | core-cli | CLI for node interaction |
| keosd | core-wallet | Key/wallet management daemon |
| spring-util | core-util | Blockchain utilities |

## Config Directory

Default config path is now `etc/core_net/` (was `etc/eosio/`).

## Plugin Names

All plugins use the `core_net::` namespace prefix (was `eosio::`):

| Old | New |
|-----|-----|
| eosio::producer_plugin | core_net::producer_plugin |
| eosio::chain_api_plugin | core_net::chain_api_plugin |
| eosio::net_plugin | core_net::net_plugin |
| eosio::http_plugin | core_net::http_plugin |
| eosio::state_history_plugin | core_net::state_history_plugin |
| eosio::db_size_api_plugin | core_net::db_size_api_plugin |
| eosio::trace_api_plugin | core_net::trace_api_plugin |
| eosio::prometheus_plugin | core_net::prometheus_plugin |
| eosio::resource_monitor_plugin | core_net::resource_monitor_plugin |

The pattern applies to all plugins: `eosio::` becomes `core_net::`.

## Basic Startup

```bash
core_netd --config-dir etc/core_net/node_00 \
          --data-dir data/core_net/node_00 \
          --plugin core_net::chain_api_plugin \
          --plugin core_net::net_plugin \
          --plugin core_net::producer_plugin \
          --http-server-address 0.0.0.0:8888
```

## Key Management

Start the wallet daemon and create/import keys:

```bash
core-wallet --http-server-address 127.0.0.1:8900
core-cli wallet create --to-console
core-cli wallet import --private-key <key>
```

## Snapshots

| Operation | Command |
|-----------|---------|
| Take snapshot | `core-cli producer take_snapshot` |
| Restore from snapshot | `core_netd --snapshot <path>` |

Snapshot version is now v9. The node reads v2 through v8 snapshots for backward compatibility.

## Common CLI Commands

```bash
core-cli get info                          # Chain info
core-cli get block <num>                   # Get block by number
core-cli get account <name>                # Account details
core-cli push action <contract> <action> '<json>' -p <auth>
core-cli system newaccount <creator> <name> <owner-key> <active-key>
```
