# What's New in Anvo Core

Anvo Core is a high-performance Layer 1 blockchain node, forked from [Spring](https://github.com/AntelopeIO/spring) (Antelope/EOSIO). It is fully rebranded for the Anvo Network while maintaining complete backward compatibility with existing Antelope-ecosystem contracts and infrastructure.

## Highlights

- **`core_net::` namespace** — All internal code uses `core_net::` as primary namespace
- **`core.*` system accounts** — Default genesis uses `core`, `core.token`, `core.system`, etc.
- **Dual WASM intrinsic export** — Exports both `core_net_*` and `eosio_*` host function names
- **Full Antelope contract compatibility** — Existing contracts run unmodified
- **Savanna consensus** — HotStuff-based BFT with BLS12-381 finality (inherited from Spring)
- **AArch64 support** — Full ARM64 OC-compiled WASM execution

## Security Improvements

### Persistent node identity

Nodes now have a persistent cryptographic identity. On first startup, a secp256k1 keypair is generated and saved to `data-dir/p2p-node-key` (0600 permissions). The `node_id` used in P2P handshakes is derived deterministically as `SHA256(public_key)`, making it stable across restarts and cryptographically bound to the key.

Previously, `node_id` was a random value regenerated on every startup — it couldn't be used to verify a node's identity and changed on each restart. Now the private key signs the ECDH ephemeral key during encrypted handshake, proving the node owns its advertised identity. This is the foundation for future authenticated peering ([#92](https://github.com/AnvoIO/core/issues/92)).

### P2P encrypted transport

All P2P connections now support ChaCha20-Poly1305 encrypted transport with ECDH (X25519) key exchange. Enable with:

```ini
p2p-enable-encryption = true        # enable encryption (negotiate with peers)
p2p-require-encryption = true       # reject unencrypted connections
```

Encrypted nodes automatically negotiate with peers — if both sides support encryption, the connection is encrypted; otherwise it falls back to plaintext (unless `require-encryption` is set). Fully backward compatible with Spring V1 nodes.

**v0.1.3-alpha** makes encrypted P2P actually usable. It fixes three encrypted-pipeline defects that together prevented real-world use:

1. **Nonce/wire-order drift (#98).** The AEAD nonce was assigned at enqueue time, but the connection's three priority queues (block-sync, general, trx) drained messages in a different order, producing out-of-order nonces on the wire. Peers rejected the mismatched nonces with "Message decryption/authentication failed" after the first batch of blocks. Fix: the `seal` call moved into the wire-order drain path (`fill_out_buffer`) so nonces and transmission order always match.
2. **Parallel receive pipeline (#98).** The encrypted receive path dispatched `signed_block` / `packed_transaction` / `vote_message` directly to `handle_message`, bypassing the specialized `process_next_*_message` functions that own sync bookkeeping (`sync_recv_block`, `have_block` dedup, below-LIB drops, WebAuthn rejection, trx-drop-while-syncing). With the nonce fix in place, sync stalled after the first batch because `sync_next_expected_num` never advanced. Fix: collapse to a single dispatch path — the encrypted branch is now pure transport (decrypt bytes; hand off to `process_next_message`), and the specialized handlers take a plaintext byte span so both paths share the same bookkeeping.
3. **Close-time encryption state leak (#102).** `connection::_close()` reset most per-connection state but not the encrypted-transport session state (`encryption_ctx`, `ephemeral_ecdh_key`, `encryption_active`, `encryption_transitioning`, `encryption_handshake_sent`, `encryption_start_time`, `pending_peer_key_exchange`). On reconnect, the outbound `connection_ptr` was reused with its stale AEAD context, so outgoing handshakes were sealed with keys the peer's fresh connection couldn't decrypt — producing random `Invalid net_message index` parse errors on the peer and an unrecoverable reconnect loop after any sync-response-timer stall on single-peer setups. Fix: reset all encrypted-transport state in `_close()` alongside the other per-connection fields, so every reconnect starts a fresh ECDH key exchange.

v0.1.2-alpha operators should upgrade before enabling encryption.

### Sync catch-up skips subjective CPU on deeply finalized blocks

**v0.1.4-alpha** fixes a permanent sync stall on slow or contested hardware ([#104](https://github.com/AnvoIO/core/issues/104)).

During sync catch-up, blocks received from peers were applied with `block_status::complete`, which leaves subjective per-account CPU validation active. On hardware slower than the original producer — or on any hardware during an unfortunate OC tier-up window — a historic transaction's local wall-clock CPU usage could exceed the account's per-transaction CPU limit (which reflects what the producer recorded, not what the local machine measures). The resulting `tx_cpu_usage_exceeded` rejection tripped `block_status_monitor_` and closed the peer connection, which reopened and re-fetched the same failing block — an unrecoverable reconnect loop.

The fix: when a peer has reported a `fork_db_root_num` (network LIB) more than `deep_sync_lib_margin_blocks` (1000 blocks, ~8.3 minutes of chain time) past the block being applied, that block is unambiguously network-finalized. Those blocks are applied as `validated` rather than `complete`, so `skip_trx_checks()` bypasses the subjective CPU and auth checks — matching the semantics of replay from the on-disk block log. `force_all_checks=true` still overrides via the existing gate in `light_validation_allowed()`, preserving the forensic-replay escape hatch. Consensus-critical validation (block structure, QC signatures when `force_all_checks`, protocol features) is unchanged.

net_plugin updates the controller's peer-LIB high-water mark monotonically — a single dropped or misbehaving peer cannot rewind it.

### API listener separation

Sensitive management APIs (`producer_api_plugin`, `net_api_plugin`) now bind to a separate listener from public read-only APIs by default. This prevents accidental exposure of admin endpoints to the public internet.

### Cryptographic hardening

- Constant-time comparison for all signature and key operations (prevents timing side-channels)
- Automatic key erasure — private keys are securely zeroed from memory after use
- Wallet auto-lock timeout and CLI input guards
- Plugin shutdown ordering hardened to prevent key material leaks

### `FILE:` signature provider

A new `FILE:` signature provider type loads block signing keys from a file instead of passing them on the command line:

```ini
signature-provider = PUB_KEY=FILE:/path/to/private.key
```

The key file must have owner-only permissions (0600 or 0400). This replaces the `KEY:` provider, which exposes private keys in process arguments visible via `ps`, `/proc/PID/cmdline`, shell history, and audit logs.

### `KEY:` signature provider deprecated

The `KEY:` signature provider now logs an **error-level** warning at startup. It will be removed in a future release. Migrate to `FILE:` or `CORE_WALLET:` before then.

### Consensus validation

Proposer schedule version is now validated in the Instant Finality (IF) code path, preventing blocks with stale or incorrect schedule versions from being accepted.

### JIT hardening

The core-vm JIT compiler includes additional safety checks: branch target overflow validation, corrected stack size formula, signal handling tests, and OC bounds checking.

### Protocol feature auto-detection

Stale protocol feature JSON files are automatically detected and regenerated on startup, preventing nodes from failing to activate features after binary upgrades.

## Protocol Features

### Dual protocol feature digests

Anvo Core maintains both the original `eosio::` protocol feature digests (for compatibility with existing chains) and new `CORE_*` variants. Both sets are recognized — existing chains continue to use the original digests, while new chains can activate features using either set. VM OC gracefully falls back to interpreted execution when OC compilation is unavailable.

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
