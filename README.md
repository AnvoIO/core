# Anvo Network Core

High-performance Layer 1 blockchain node software. A fork of [Spring](https://github.com/AntelopeIO/spring) (Antelope/EOSIO) rebranded for Anvo Network, featuring Savanna consensus -- a HotStuff-based BFT protocol delivering 2-3 second finality. Full smart contract compatibility with existing EOSIO chains; no recompilation required.

## Key Features

- **Savanna consensus** with BLS12-381 finality (HotStuff-based BFT)
- **WASM smart contract runtime** -- interpreter and JIT execution
- **Full EOSIO contract compatibility** -- existing contracts run unmodified, no recompilation needed
- **Genesis-configurable system accounts** -- deploy with `core.*` or `eosio.*` prefixes
- **Plugin-based architecture** -- 22+ plugins for modular node configuration
- **REST API infrastructure** for chain interaction and monitoring

## Executables

| Binary | Description |
|---|---|
| `core_netd` | Blockchain node daemon. Runs the chain, processes blocks, and serves API requests. |
| `core-cli` | Command-line interface for interacting with the node (transactions, queries, account management). |
| `core-wallet` | Key management and transaction signing daemon (keosd replacement). |
| `core-util` | Blockchain utility tools: block log operations, snapshots, BLS key generation. |

## Quick Start

### Build from Source

```bash
git clone --recursive https://github.com/Anvo-Network/core.git
cd core
mkdir build && cd build
cmake -DENABLE_OC=OFF -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

> **Note:** `-DENABLE_OC=OFF` disables the OC tier compiler, which requires LLVM 7-11. The OC dependency is being modernized to support current LLVM versions. Interpreter and JIT modes remain fully functional without it.

### Build Requirements

- C++20 compiler and standard library
- CMake 3.16+
- libcurl 7.40.0+
- git, GMP, Python 3, python3-numpy, zlib

### Verify Installation

```bash
core_netd --full-version
```

## Supported Platforms

| Platform | Status | Tested With |
|---|---|---|
| Ubuntu 24.04 Noble | Primary | GCC 13.3, CMake 3.28 |
| Ubuntu 22.04 Jammy | Supported | GCC 11+ |
| Other Linux distributions | Best-effort | -- |

## Genesis Configuration

New chains can configure system account names via the `system_account_prefix` field in `genesis.json`. Setting the prefix to `core` produces system accounts like `core.token`, `core.msig`, etc. Setting it to `eosio` (or omitting) preserves standard EOSIO account names for backward compatibility.

See the `_research/` directory for detailed documentation.

## EOSIO Compatibility

Anvo Network maintains full backward compatibility with existing EOSIO smart contracts:

- **WASM intrinsics** are dual-registered under both names (`eosio_assert` and `core_net_assert`), so contracts compiled for either convention execute correctly.
- **ABI version strings** are accepted in both `eosio::abi/1.x` and `core_net::abi/1.x` formats.
- **System contracts** compiled for `eosio.*` accounts work without modification when the genesis is configured with the `eosio` prefix.

No contract recompilation is needed to migrate from an EOSIO-based chain.

## License

[Business Source License 1.1](LICENSE) (BSL 1.1). Source-available; converts to Apache License 2.0 after 3 years. See the `LICENSE` file for full terms.

## Links

- **GitHub:** https://github.com/Anvo-Network/core
- **Research docs:** [`_research/`](_research/) directory
