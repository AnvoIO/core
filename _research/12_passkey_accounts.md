# Passkey-First Account Model

## Vision

Users create accounts with Face ID / fingerprint / Windows Hello. No seed phrases,
no browser extensions, no wallet apps. The device IS the wallet. The biometric IS
the key.

This isn't a wrapper or abstraction layer — it's built into the protocol. Every
account natively supports passkeys, multi-device management, hierarchical permissions,
and mixed key types.

## What Already Exists in Spring

### Three Key Types (Protocol-Level)

| Type | Curve | Use Case | Prefix |
|------|-------|----------|--------|
| K1 | secp256k1 | Legacy, Bitcoin/Ethereum compatible | `PUB_K1_` / `SIG_K1_` |
| R1 | secp256r1 (P-256) | Modern ECDSA, passkey base curve | `PUB_R1_` / `SIG_R1_` |
| WA | WebAuthn (P-256 + metadata) | Full passkey with RPID + user presence | `PUB_WA_` / `SIG_WA_` |

WebAuthn key type is defined as protocol feature `WEBAUTHN_KEY` with full
implementation: signature verification, RPID validation, user presence/verification
flags, 40+ unit tests.

### WebAuthn Verification Flow (Already Implemented)

```
1. Parse client JSON → extract challenge field
2. base64url decode challenge → verify sha256(challenge) == transaction digest
3. Validate origin starts with "https://"
4. Extract RPID from origin (domain without scheme/port)
5. Verify sha256(RPID) == auth_data[0:32] (RPID hash in authenticator data)
6. Check user presence flag (auth_data[32] & 0x01)
7. Check user verification flag (auth_data[32] & 0x04) — biometric/PIN was used
8. Compute signed_digest = sha256(auth_data || sha256(client_json))
9. ECDSA P-256 recovery → extract public key
10. Match against account's permission authority → authorized
```

**Files:**
- `libraries/libfc/include/fc/crypto/elliptic_webauthn.hpp`
- `libraries/libfc/src/crypto/elliptic_webauthn.cpp`
- `libraries/libfc/test/crypto/test_webauthn.cpp`

### Permission System (Already Implemented)

EOSIO has the most powerful permission system of any blockchain:

```cpp
struct authority {
    uint32_t threshold;                         // weight required
    vector<key_weight> keys;                    // keys with weights
    vector<permission_level_weight> accounts;   // nested permissions with weights
    vector<wait_weight> waits;                  // time-delay conditions with weights
};
```

**Features:**
- **Hierarchical**: owner → active → custom (owner can always recover active)
- **Weighted threshold**: any combination of keys/accounts/delays can satisfy
- **Mixed key types**: K1 + R1 + WA keys in the same permission
- **Nested permissions**: one account's permission can reference another account
- **Time delays**: require waiting period before execution (for high-value ops)

No other chain has this at the protocol level. Ethereum approximates it with
ERC-4337 smart contract wallets, but those are contract-level workarounds.

## New User Onboarding Flow

### Current (Broken)

```
1. User wants to use a dApp
2. Needs an account → someone with an existing account must create it
3. That someone must pay RAM for the new account
4. User needs to stake tokens for CPU/NET before transacting
5. User needs a wallet app that manages private keys
6. User writes down a 12/24 word seed phrase
7. User can finally transact
```

Time to first transaction: **minutes to hours**. Most users bounce.

### New (With Passkeys + Gas)

```
1. User visits dApp website
2. Clicks "Create Account"
3. Browser prompts: "Create a passkey for [dApp]?"
4. User touches Face ID / fingerprint / Windows Hello
5. Account created (dApp sponsors gas for creation, or faucet)
6. User can transact immediately
```

Time to first transaction: **5 seconds**. No seed phrase. No wallet download.
No staking tutorial.

### Technical Flow

```
User's Device                    dApp Backend                    Chain
     |                               |                            |
     | 1. navigator.credentials      |                            |
     |    .create({                   |                            |
     |      rp: {name: "dApp"},       |                            |
     |      user: {name: "alice"},    |                            |
     |      pubKeyCredParams:         |                            |
     |        [{alg: -7, type: "pk"}] | ← P-256 (ES256)           |
     |    })                          |                            |
     |                               |                            |
     | 2. Device creates key pair     |                            |
     |    Biometric prompt shown      |                            |
     |    Returns: public key +       |                            |
     |    credential ID + attestation |                            |
     |                               |                            |
     |---- public key -------------→ |                            |
     |                               | 3. Build createaccount tx: |
     |                               |    name: hash(pubkey)[0:12] |
     |                               |    owner: {WA key, weight 1}|
     |                               |    active: {WA key, wt 1}  |
     |                               |    gas_payer: dApp account  |
     |                               |                            |
     |                               |---- push transaction ----→ |
     |                               |                            | 4. Create account
     |                               |                            |    Set permissions
     |                               |                            |    Deduct gas from dApp
     |                               |                            |
     |                               | ←--- confirmation -------- |
     |                               |                            |
     | ←-- "Account ready!" -------- |                            |
     |                               |                            |
     | 5. User can now transact       |                            |
     |    Face ID signs each tx       |                            |
```

## Daily Usage

### Transacting

```
User clicks "Send 10 TOKENS to bob"
    → dApp builds transaction
    → browser prompts: "Sign with your passkey?"
    → user touches Face ID / fingerprint
    → device signs transaction with P-256 WebAuthn assertion
    → signature = compact_sig + auth_data + client_json
    → transaction submitted to chain
    → node verifies WA signature, checks permission, executes
    → if baseline allocation covers it: free
    → if not: gas deducted from user or dApp (per gas model)
```

No wallet app. No popups asking for gas approval. No hex addresses to copy.

### Multi-Device Management

EOSIO's permission system makes this trivial:

```
Account "alice":
  owner (threshold: 1)
    - WA key [iPhone passkey, RPID: "dapp.com", weight: 1]
    - WA key [MacBook passkey, RPID: "dapp.com", weight: 1]
    - K1 key [paper backup, weight: 1]
  active (threshold: 1)
    - WA key [iPhone passkey, RPID: "dapp.com", weight: 1]
    - WA key [MacBook passkey, RPID: "dapp.com", weight: 1]
```

**Add a device:**
1. Settings → "Add Device"
2. New device creates passkey
3. Existing device signs `updateauth` to add new WA key to active permission

**Remove a device:**
1. Settings → "Remove Device"
2. Any remaining device signs `updateauth` to remove the old key

**Lost phone:**
1. MacBook passkey still works → rotate active to remove lost phone's key
2. OR: paper backup → sign `updateauth` via owner permission

### Advanced Permission Patterns

**Spending limits (time-delayed):**
```
Account "alice":
  active (threshold: 1)
    - WA key [phone, weight: 1]          ← instant for normal txns
  highvalue (threshold: 2)
    - WA key [phone, weight: 1]
    - wait [24 hours, weight: 1]         ← large transfers require 24hr delay
```

**Multi-sig with passkeys:**
```
Account "company_treasury":
  active (threshold: 2)
    - WA key [alice's phone, weight: 1]
    - WA key [bob's phone, weight: 1]
    - WA key [carol's phone, weight: 1]
  ← any 2 of 3 executives must approve
```

**dApp-scoped permissions:**
```
Account "alice":
  active (threshold: 1)
    - WA key [phone, weight: 1]
  game_permission (threshold: 1, linked to game contract actions only)
    - WA key [phone, weight: 1]  ← auto-approve game actions
```

## Key Recovery

### Hierarchical Recovery (Built-In)

Owner permission always supersedes active. This is the native recovery mechanism:

| Scenario | Recovery Path |
|---|---|
| Lost phone, have laptop | Laptop passkey rotates active key |
| Lost all devices, have paper backup | Paper backup (K1 key) rotates owner + active |
| Lost phone, no backup | Social recovery (see below) |

### Social Recovery (Contract-Level)

A smart contract that manages guardian-based recovery:

```
Recovery contract for "alice":
  guardians: [bob, carol, dave]
  threshold: 2 of 3
  delay: 48 hours (safety window for alice to cancel)

Recovery process:
  1. bob calls recovery_contract::initiate(alice, new_WA_pubkey)
  2. carol calls recovery_contract::approve(alice)
  3. 48-hour delay starts (alice can cancel if she still has access)
  4. After delay: contract calls updateauth(alice, active, new_WA_key)
  5. alice creates new passkey, regains access
```

No node changes needed — the `core.code` virtual permission already allows
contracts to act on behalf of accounts with explicit authorization.

### Passkey Sync (Platform-Level)

Modern platforms sync passkeys across devices automatically:
- **Apple**: iCloud Keychain syncs passkeys across iPhone, iPad, Mac
- **Google**: Google Password Manager syncs across Android, Chrome
- **Microsoft**: Windows Hello syncs via Microsoft account

This provides automatic backup without the user doing anything. Lose your phone,
sign in on a new one, passkeys are restored from cloud.

**Important:** This is platform behavior, not chain behavior. The chain doesn't
know or care how the user's device manages the key. It just verifies the signature.

## Account Names

### The Problem

EOSIO requires 1-12 character names from charset `[a-z1-5.]`. Users don't want
to choose `bob12345.....` or figure out what's available.

### Solutions

**Option 1: Hash-Based Names (Recommended for Default)**

Derive account name from public key hash:
```
account_name = base32_encode(sha256(WA_pubkey)[0:8])
// Produces: "a1b2c3d4e5f6" (12 chars, deterministic)
```

User never sees or types this. The dApp shows a display name or address.
Under the hood, it's a valid EOSIO account name derived from the passkey.

**Option 2: Human-Readable Aliases (Name Service)**

A name service contract (like ENS on Ethereum):
```
alice.anvo → a1b2c3d4e5f6 (underlying account)
```

Optional layer. Users who want a readable name register one.
Users who don't care never think about it.

**Option 3: Extended Account Names (Protocol Change)**

Expand account name format to support longer strings or more characters.
This is a protocol-level change — significant but doable. Would break
backward compatibility with existing EOSIO tooling.

**Recommendation:** Option 1 (hash-based) as default, Option 2 (name service)
as optional. Don't change the account name format — it's not worth the
compatibility cost.

## Implementation

### What Already Works (Zero Changes Needed)

- WebAuthn signature creation and verification
- P-256 key type (R1) and WebAuthn key type (WA)
- Permission system with weighted thresholds and mixed key types
- Key rotation via `updateauth`
- Hierarchical recovery (owner → active)
- `core.code` virtual permission for contract-based recovery

### What Needs to Be Built

| Component | Where | Effort | Dependencies |
|---|---|---|---|
| Permissionless account creation | System contract + gas model | Included in gas work | Gas model (doc 11) |
| Hash-based account name derivation | System contract | 1 week | None |
| Name service contract | Smart contract | 1-2 weeks | None |
| Social recovery contract | Smart contract | 1-2 weeks | None |
| TypeScript SDK with WebAuthn | SDK (TypeScript) | 2-3 weeks | None |
| Reference auth UI (passkey creation, device management) | Web (React/similar) | 2-4 weeks | SDK |
| Multi-device management UX | Web UI | 1-2 weeks | SDK |
| Documentation + developer guides | Docs | 1-2 weeks | SDK |
| **Total** | | **~2-3 months** | |

### Integration with Gas Model

Passkey accounts and the gas model (doc 11) are complementary:

```
New user creates passkey account (dApp pays gas for creation)
    → account exists with free baseline allocation
    → user transacts within baseline (free)
    → exceeds baseline → gas auto-deducted from token balance
    → OR dApp sponsors gas for all interactions
```

The user experience: create account with Face ID, start using the dApp,
never think about resources or fees.

## Competitive Position

| Feature | Anvo | Ethereum | Solana | Aptos | Sui |
|---|---|---|---|---|---|
| **Native passkey support** | Protocol-level | ERC-4337 (contract) | No | No | zkLogin (ZK, partial) |
| **Multi-device keys** | Permission system | Smart wallet only | No | No | No |
| **Hierarchical permissions** | owner→active→custom | Smart wallet only | No | No | No |
| **Mixed key types** | K1+R1+WA weighted | No (one key per EOA) | No | No | No |
| **Social recovery** | Contract + permissions | Smart wallet only | No | No | No |
| **Time-delayed ops** | wait_weight native | Smart wallet only | No | No | No |
| **No seed phrase** | Yes (passkey-first) | Smart wallets only | No | No | zkLogin only |
| **5-second onboarding** | Yes (passkey + gas) | No | No | No | Partial |

Ethereum gets closest with ERC-4337 + smart contract wallets, but:
- Every smart wallet is a separate contract deployment (~$5-20 to create)
- Smart wallet signatures are more expensive to verify (contract call overhead)
- No standard — Safe, Biconomy, ZeroDev all incompatible
- EOA keys still can't be passkeys natively

Anvo has all of this **at the protocol level**. Every account is a smart
wallet by default. No wrapper contracts. No abstraction layers. No fragmented
standards.

## The Pitch

**"Create an account with Face ID. No seed phrase. No wallet download.
Free transactions. Lose your phone? Touch your laptop. Lose everything?
Your friends recover your account. All built into the protocol."**

No other chain can say this.
