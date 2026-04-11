# Anvo Network Core

> **[v0.1.1-alpha released](https://github.com/AnvoIO/core/releases/tag/v0.1.1-alpha)** — Ubuntu 24.04 packages for x86_64 and ARM64

High-performance Layer 1 blockchain node software. A fork of [Spring](https://github.com/AntelopeIO/spring) (Antelope/EOSIO) rebranded for Anvo Network, featuring BFT consensus with BLS12-381 finality delivering 2-3 second finality. Full smart contract compatibility with existing EOSIO chains; no recompilation required.

## Key Features

- **BFT consensus** with BLS12-381 finality (HotStuff-based)
- **WASM smart contract runtime** — interpreter, JIT, and VM OC (Optimized Compiler) on both x86_64 and ARM64
- **Native AArch64 support** — full test parity on ARM64 including VM OC with LLVM-based tier-up
- **Full EOSIO contract compatibility** — existing contracts run unmodified, `eosio::` namespace aliases maintained
- **Genesis-configurable system accounts** — deploy with `core.*` or `eosio.*` prefixes
- **Modernized dependencies** — Boost 1.90, secp256k1 v0.7.1, BoringSSL Feb 2026, musl 1.2.5 (CDT)
- **Plugin-based architecture** — 22+ plugins for modular node configuration

## Install

Ubuntu 24.04 `.deb` packages for x86_64 and ARM64 are available from the [releases page](https://github.com/AnvoIO/core/releases):

```bash
# x86_64
sudo apt install ./anvo-core_0.1.1-alpha-ubuntu24.04_amd64.deb

# ARM64
sudo apt install ./anvo-core_0.1.1-alpha-ubuntu24.04_arm64.deb
```

For Docker-based deployment with automated configuration, snapshot management, and monitoring, see **[core-node](https://github.com/AnvoIO/core-node)**.

## Executables

| Binary | Description |
|---|---|
| `core_netd` | Blockchain node daemon. Runs the chain, processes blocks, and serves API requests. |
| `core-cli` | Command-line interface for interacting with the node (transactions, queries, account management). |
| `core-wallet` | Key management and transaction signing daemon. |
| `core-util` | Blockchain utility tools: block log operations, snapshots, BLS key generation. |

## Build from Source

```bash
git clone --recursive https://github.com/AnvoIO/core.git
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DLLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm -GNinja
cmake --build build
sudo cmake --install build
```

### Build Requirements

- Ubuntu 24.04 (primary) or 22.04
- GCC 13+ (C++20)
- CMake 3.16+
- LLVM 14–19
- libcurl, libgmp, zstd, python3-numpy, zlib

### Verify Installation

```bash
core_netd --full-version
```

## Supported Platforms

| Platform | Architecture | Status |
|---|---|---|
| Ubuntu 24.04 Noble | x86_64 | Primary — CI tested, packages available |
| Ubuntu 24.04 Noble | ARM64 (AArch64) | Primary — CI tested, packages available |
| Ubuntu 22.04 Jammy | x86_64 | Supported |
| Other Linux | — | Best-effort |

## Genesis Configuration

New chains configure system account names via `system_account_prefix` in `genesis.json`:

```json
{
  "initial_timestamp": "2026-01-01T00:00:00.000",
  "initial_key": "PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV",
  "system_account_prefix": "core"
}
```

- `"core"` — system accounts are `core`, `core.token`, `core.msig`, etc.
- Omitted — defaults to `eosio` for backward compatibility with existing chains

## EOSIO Compatibility

Anvo Network maintains full backward compatibility with existing EOSIO smart contracts:

- **C++ namespace aliases** — `eosio::` is aliased to `core_net::` throughout; existing code compiles without changes
- **WASM intrinsics** are dual-registered (`eosio_assert` / `core_net_assert`), so contracts compiled for either convention execute correctly
- **ABI version strings** accepted in both `eosio::abi/1.x` and `core_net::abi/1.x` formats
- **Key formats** — modern `PUB_K1_`/`PVT_K1_`/`SIG_K1_` is the default output; legacy `EOS`-prefixed keys are accepted as input everywhere

All new development is in the `core_net::` namespace. The `eosio::` aliases are frozen — maintained for compatibility but will not receive new additions.

## Security

Anvo Core has undergone a comprehensive security audit covering all modules: cryptography, consensus, P2P networking, wallet, API, VM runtime, and CLI tools. The full audit report is available at [`security/AUDIT-2026-04.md`](security/AUDIT-2026-04.md).

Key security features:

- **P2P encrypted transport** — AES-256-GCM with ECDH key exchange, backward compatible with Spring V1
- **API listener separation** — sensitive management APIs isolated from public endpoints
- **`FILE:` signature provider** — load block signing keys from permission-checked files instead of command-line arguments
- **Cryptographic hardening** — constant-time comparisons, secure key erasure, timing side-channel mitigations

To report a security vulnerability, please open an issue at https://github.com/AnvoIO/core/issues.

## License

[Business Source License 1.1](LICENSE) (BSL 1.1). Existing EOSIO/Antelope chains are expressly permitted to adopt this codebase. Converts to Apache License 2.0 three years after v1.0.0. See [LICENSE](LICENSE) for full terms.

## Community

We welcome collaboration — reach out at **community@anvo.io**.

## Links

- **Releases:** https://github.com/AnvoIO/core/releases
- **Node Deployment (Docker):** https://github.com/AnvoIO/core-node
- **CDT (Contract Development Toolkit):** https://github.com/AnvoIO/cdt
- **Issue tracker:** https://github.com/AnvoIO/core/issues
