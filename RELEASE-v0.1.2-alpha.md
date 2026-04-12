# Anvo Core v0.1.2-alpha Release Notes

**Release date**: 2026-04-12
**Previous release**: v0.1.1-alpha (2026-03-28)

---

## Overview

v0.1.2-alpha is a security hardening and code quality release. It delivers the results of a comprehensive security audit (47 findings across 8 modules), introduces encrypted P2P transport, and completes a significant legacy code cleanup. A full chain replay from genesis (112.5M+ blocks) validated zero consensus regressions.

## Security Audit

A full codebase security audit was performed covering cryptography, consensus, P2P networking, wallet, API, VM runtime, and CLI tools. All 47 findings have been addressed: 39 fixed in code, 6 documented as by-design, 2 accepted with existing mitigations.

The complete audit report is published at [`security/AUDIT-2026-04.md`](security/AUDIT-2026-04.md).

### New Security Features

| Feature | Description |
|---------|-------------|
| **P2P encrypted transport** | AES-256-GCM with ECDH key exchange. Enable with `p2p-enable-encryption = true`. Backward compatible with Spring V1 nodes. |
| **Persistent node identity** | Persistent secp256k1 keypair at `data-dir/p2p-node-key`. Node ID is deterministic (`SHA256(pubkey)`), stable across restarts, signed during ECDH handshake. |
| **API listener separation** | Sensitive management APIs (`producer_api_plugin`, `net_api_plugin`) bind to a separate listener from public APIs by default. |
| **`FILE:` signature provider** | Load block signing keys from permission-checked files instead of command-line arguments. `--signature-provider PUB=FILE:/path/to/key` (file must be 0600/0400). |
| **`KEY:` provider deprecated** | `KEY:` signature provider now logs an **error-level** warning. Migrate to `FILE:` or `CORE_WALLET:`. `KEY:` will be removed in a future release. |
| **PBKDF2 wallet encryption** | Wallet encryption upgraded from raw SHA-512 hash to PBKDF2 (100K iterations, random salt). Existing wallets auto-migrate on unlock. |
| **Secure key erasure** | All private key types (ECDSA, BLS, WIF) are zeroed with `OPENSSL_cleanse()` on destruction. |
| **Constant-time comparisons** | All hash and signature comparisons use constant-time operations to prevent timing side-channels. |
| **P2P slow-loris protection** | Configurable incomplete-message read deadline (`p2p-incomplete-message-timeout-ms`). |
| **P2P memory cap** | Global buffer memory ceiling across all connections (`p2p-max-total-buffer-bytes`). |
| **P2P access control** | ACL deny/allow lists, CIDR filtering, peer reputation tiers. |

### Consensus Validation

- Proposer schedule version validated in the Instant Finality code path
- All bare `assert(0)` statements replaced with throwing assertions (not compiled out in release builds)
- ABI deserialization: hard maximum array size (8,192), overflow detection in fixed-size array parsing
- VM: table element bounds check, JIT stack offset overflow check, function index validation

## Legacy Code Cleanup

### Rebranding

- All CMake build messages updated from "Spring" to "Anvo Core"
- All user-facing error messages, CLI descriptions, and comments updated from `keosd`/`nodeos`/`cleos` to `core-wallet`/`core_netd`/`core-cli`
- Default P2P agent name: "Anvo Core Agent"
- CLI app descriptions updated
- Install paths renamed: `spring_boost` → `core_boost`, `springboringssl` → `coreboringssl`
- Dead `spring-config.cmake.in` file removed

### Build System

- Deep mind version string injected from CMake instead of hardcoded (`CORE_NET_DM_VERSION_STR`)
- EOSIO test wasms repo forked to [`AnvoIO/core-vm-test-wasms`](https://github.com/AnvoIO/core-vm-test-wasms)
- `EOSIO Developer Options` → `Core Developer Options` in build warnings

### Dead Code Removal

- All `#warning` directives removed
- All `#if 0` blocks removed
- Stale `TODO`/`FIXME`/`HACK` comments resolved: fixed, filed as issues, or removed
- Wallet import merge feature implemented (previously a TODO)

### Documentation

- `WHATS_NEW.md` updated with all security features shipped since v0.1.1-alpha
- `README.md` Security section added with link to audit report
- Producer plugin docs updated: `FILE:` provider documented, `KEY:` marked deprecated
- Producing node guide rewritten to recommend `FILE:` over `KEY:`
- Bios boot tutorial updated for Anvo binary names and repo URLs

## Protocol Features

### Dual protocol feature digests

Both original `eosio::` and new `CORE_*` protocol feature digests are recognized. Existing chains use original digests; new chains can use either set.

### Protocol feature auto-detection

Stale protocol feature JSON files are automatically detected and regenerated on startup.

## Bug Fixes

- Flaky `p2p_encrypted_transport_test` timeout increased from 60s to 120s (#95)
- JIT hardening: branch target overflow validation, stack size formula correction
- ABI: re-enabled null-element validation for non-optional array types
- VM: negative data segment offset check added
- Snapshot: TOCTOU race in file operations fixed

## Chain Replay Validation

Full genesis sync of Libre testnet completed:
- **112.5M+ blocks** from genesis (July 2022) through present
- **Zero consensus errors**
- Binary built from this release branch
- Validates all security hardening changes are consensus-safe

## Interoperability

Tested against Spring v1.2.2:

| Test | Result |
|------|--------|
| Anvo V2 ↔ Spring V1 plaintext sync | PASS |
| Anvo V2 encrypted, Spring V1 plaintext — graceful fallback | PASS |
| `p2p-require-encryption` rejects Spring V1 peer | PASS |

## Known Issues

| Issue | Description |
|-------|-------------|
| [#96](https://github.com/AnvoIO/core/issues/96) | Multi-peer P2P sync is bottlenecked by slowest peer. Use single fast peer for initial sync. |
| [#95](https://github.com/AnvoIO/core/issues/95) | `p2p_encrypted_transport_test` intermittently times out on CI (fixed in this release) |
| [#94](https://github.com/AnvoIO/core/issues/94) | `EOS_ASSERT` macros not yet renamed to `CORE_ASSERT` (1,145 call sites) |
| [#26](https://github.com/AnvoIO/core/issues/26) | `docs/01_nodeos/` directory still uses legacy naming (~250 occurrences) |

## Upgrade Notes

### For node operators

- **Signature provider**: If using `KEY:` in `--signature-provider`, migrate to `FILE:` before the next release. Create a key file with `echo "YOUR_PRIVATE_KEY" > /path/to/key && chmod 600 /path/to/key`, then use `--signature-provider PUB=FILE:/path/to/key`.
- **P2P encryption**: Enable with `p2p-enable-encryption = true` in config.ini. Optional but recommended for producer nodes.
- **Wallet auto-migration**: First unlock after upgrade will auto-migrate wallet encryption to PBKDF2. A backup of the original wallet file is preserved.

### For developers

- No API changes. All HTTP endpoints unchanged.
- `WHATS_NEW.md` documents all changes since v0.1.1-alpha.

## Build

```bash
git clone --recursive -b v0.1.2-alpha https://github.com/AnvoIO/core.git
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm -GNinja
cmake --build build
```

Requires Ubuntu 24.04, GCC 13+, CMake 3.16+, LLVM 14-19.
