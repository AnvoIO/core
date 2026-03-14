# WASM ABI Compatibility Guide

Reference for contract developers on WASM and ABI compatibility after the rebrand.

## Existing Contracts Work Unmodified

Deployed contracts do not need recompilation. The runtime is fully backward compatible with all existing WASM binaries.

## Dual-Registered Intrinsics

Both legacy and current names resolve to the same implementation:

| Legacy (EOSIO) | Current (Anvo) |
|---|---|
| `eosio_assert` | `core_net_assert` |
| `eosio_assert_message` | `core_net_assert_message` |
| `eosio_assert_code` | `core_net_assert_code` |
| `eosio_exit` | `core_net_exit` |

Legacy names are permanently supported and will never be removed.

## ABI Version Strings

Both formats are accepted when parsing ABI definitions:

```
eosio::abi/1.0
eosio::abi/1.1
eosio::abi/1.2
core_net::abi/1.0
core_net::abi/1.1
core_net::abi/1.2
```

## New Intrinsics Policy

- New WASM intrinsics added in future releases will use `core_net_*` names only.
- No `eosio_*` aliases will be created for new functions.
- Contracts compiled against the new SDK should use `core_net_*` names.

## Injection Functions

Internal softfloat injection names (e.g., `_core_net_f32_add`, `_core_net_f64_mul`) are runtime-internal. Contracts never import these directly. There is no compatibility concern.

## System Accounts in Contracts

If your contract references system accounts by name (e.g., inline transfers to `"eosio.token"`), be aware that the chain's system account prefix is configurable.

Best practices:

- Use `get_self()` where possible instead of hardcoded account names.
- Query the chain for the active system token contract rather than assuming a fixed name.
- For cross-chain deployments, parameterize system account references in your contract.
