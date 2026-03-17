# What's New in Anvo Core

Anvo Core is a high-performance Layer 1 blockchain node, forked from [Spring](https://github.com/AntelopeIO/spring) (Antelope/EOSIO). It is fully rebranded for the Anvo Network while maintaining complete backward compatibility with existing Antelope-ecosystem contracts and infrastructure.

## Highlights

- **`core_net::` namespace** — All internal code uses `core_net::` as primary namespace
- **`core.*` system accounts** — Default genesis uses `core`, `core.token`, `core.system`, etc.
- **Dual WASM intrinsic export** — Exports both `core_net_*` and `eosio_*` host function names
- **Full Antelope contract compatibility** — Existing contracts run unmodified
- **Savanna consensus** — HotStuff-based BFT with BLS12-381 finality (inherited from Spring)
- **AArch64 support** — Full ARM64 OC-compiled WASM execution

## Breaking Changes

### Executable Renames

| Anvo Core | Spring / Antelope | Purpose |
|-----------|-------------------|---------|
| `core_netd` | `nodeos` | Node daemon |
| `core-cli` | `cleos` | CLI client |
| `core-wallet` | `keosd` | Wallet daemon |
| `core-util` | `leap-util` | Utility tool |

### Namespace Changes

All C++ namespaces have been ported from `eosio::` to `core_net::`. Plugin names, config options, and API endpoints retain backward compatibility where applicable.

## CDT 5.x Compatibility (REQUIRED)

Anvo Core must export dual WASM host function names to support contracts compiled with both CDT 5.x (`core_net/` headers) and CDT 4.x (`eosio/` headers):

### Host Function Dual Registration

Every WASM intrinsic must be registered under both names:

| CDT 5.x import name | CDT 4.x import name | Implementation |
|---------------------|---------------------|----------------|
| `core_net_assert` | `eosio_assert` | Same function |
| `core_net_assert_message` | `eosio_assert_message` | Same function |
| `core_net_assert_code` | `eosio_assert_code` | Same function |
| `core_net_exit` | `eosio_exit` | Same function |
| `core_net_set_contract_name` | `eosio_set_contract_name` | Same function |
| `_core_net_f32_add` | `_eosio_f32_add` | Same function |
| `_core_net_f32_sub` | `_eosio_f32_sub` | Same function |
| `_core_net_f32_mul` | `_eosio_f32_mul` | Same function |
| `_core_net_f32_div` | `_eosio_f32_div` | Same function |
| `_core_net_f64_add` | `_eosio_f64_add` | Same function |
| `_core_net_f64_sub` | `_eosio_f64_sub` | Same function |
| `_core_net_f64_mul` | `_eosio_f64_mul` | Same function |
| `_core_net_f64_div` | `_eosio_f64_div` | Same function |

Plus all softfloat conversion and comparison intrinsics (`_core_net_f32_trunc_i32s`, `_core_net_f64_promote`, etc.)

**This is a blocking requirement** — Anvo Core must export both name sets before any CDT 5.x-compiled contracts (using `core_net/` headers) can be deployed.

Contracts compiled with CDT 5.x using `eosio/` compat headers continue to import the old names and work without this change.

## System Account Changes

Anvo Core uses `core.*` as the default system account prefix:

| Anvo Core | Spring / Antelope |
|-----------|-------------------|
| `core` | `eosio` |
| `core.token` | `eosio.token` |
| `core.system` | `eosio.system` |
| `core.msig` | `eosio.msig` |
| `core.wrap` | `eosio.wrap` |

The genesis configuration determines which account names are used. Anvo Core supports both naming schemes — a node can be configured with either `core.*` or `eosio.*` system accounts.

## Migration from Spring

Anvo Core is a direct fork of Spring with namespace and branding changes. All protocol-level behavior is identical. Existing Spring infrastructure (RPC endpoints, chain state, block logs) is compatible with Anvo Core after accounting for the executable renames.

### Config file changes

Replace Spring config references:
- `nodeos` → `core_netd` in systemd units and scripts
- `cleos` → `core-cli` in CLI scripts
- `keosd` → `core-wallet` in wallet scripts

### Plugin and API compatibility

All plugin names and API endpoints retain the same functionality. Internal namespaces are renamed but the HTTP API surface is unchanged.
