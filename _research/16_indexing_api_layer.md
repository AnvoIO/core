# Indexing & API Layer — Built-In Index-API Node

## Design Decision

**Build the indexer into the node distribution as a first-class plugin.**

No Hyperion. No Elasticsearch. No RabbitMQ. No separate processes. One binary,
one database, full indexing and rich API out of the box.

Hyperion requires an Elasticsearch cluster (3+ nodes for production), RabbitMQ,
Redis, Node.js workers, and a separate indexer process. It consumes more resources
than the blockchain node itself. This is the single biggest operational pain point
in the EOSIO ecosystem and a major barrier to running infrastructure.

## API Versioning Strategy

### v1 — EOSIO Wire-Compatible (Inherited)

Existing EOSIO chain API, unchanged. Every existing SDK, wallet, explorer, and
tool works without modification.

```
/v1/chain/get_info                  — chain head, LIB, version
/v1/chain/get_block                 — block by number or ID
/v1/chain/get_block_info            — lightweight block info
/v1/chain/get_account               — account info, permissions, resources
/v1/chain/get_table_rows            — contract table queries
/v1/chain/push_transaction          — submit transaction
/v1/chain/get_required_keys         — signing key lookup
/v1/chain/get_abi                   — contract ABI
/v1/chain/get_code                  — contract code
/v1/chain/get_raw_code_and_abi      — raw code + ABI
/v1/chain/get_table_by_scope        — list tables for contract

/v1/producer/*                      — producer API (existing)
/v1/net/*                           — P2P network API (existing)
/v1/db_size/*                       — database metrics (existing)
```

No changes. Full backward compatibility. SDKs targeting these endpoints
continue to work.

v2 is skipped intentionally — Hyperion used `/v2/` for its API. Skipping
avoids confusion and signals a clean break.

### v3 — New API Namespace

All new functionality lives under `/v3/`. Purpose-built for this chain's
features: history, search, identity, gas, data vaults.

#### History

```
/v3/history/get_actions
    params: account, filter (contract:action), skip, limit, sort
    returns: paginated action list with full traces

/v3/history/get_transaction
    params: id
    returns: full transaction trace, receipt, resource usage, gas cost

/v3/history/get_block_actions
    params: block_num, filter
    returns: all actions in a block

/v3/history/get_deltas
    params: contract, table, scope, primary_key, block_range
    returns: table row change history
```

#### Search

```
/v3/search/actions
    params: query (full-text), account, contract, action_name,
            date_range, block_range, limit
    returns: matching actions with relevance ranking

/v3/search/accounts
    params: query (name prefix, public key, creator)
    returns: matching accounts
```

#### Tokens

```
/v3/tokens/balances
    params: account
    returns: all token balances across all token contracts

/v3/tokens/transfers
    params: account, token, direction (in/out/both), date_range, limit
    returns: paginated transfer history

/v3/tokens/holders
    params: contract, symbol, limit
    returns: top holders for a token
```

#### Identity

```
/v3/identity/status
    params: account
    returns: identity layers achieved, attestation count, vouch count,
             personhood level

/v3/identity/attestations
    params: account, attestor (optional), claim_type (optional)
    returns: all attestations for account

/v3/identity/vouches
    params: account
    returns: who vouched for this account, who this account vouched for

/v3/identity/reputation
    params: account
    returns: reputation dimensions (age, tx volume, governance participation, etc.)
```

#### Gas

```
/v3/gas/prices
    returns: current gas prices (cpu, net, ram)

/v3/gas/usage
    params: account, date_range
    returns: gas payment history, total spent

/v3/gas/estimate
    params: transaction (unsigned)
    returns: estimated gas cost for the transaction

/v3/gas/stats
    returns: network-wide gas statistics (total collected, burned, distributed)
```

#### Accounts

```
/v3/accounts/deposits
    params: account
    returns: deposit status, maturity progress, refund eligibility

/v3/accounts/created_by
    params: account
    returns: who created this account, sponsor info

/v3/accounts/resources
    params: account
    returns: baseline allocation, staked allocation, gas balance,
             current usage, available capacity
```

#### Data (Future — Phase 4)

```
/v3/data/vault/fields
    params: account
    returns: list of data fields in vault (names + access status, not values)

/v3/data/vault/get
    params: account, field, requester (must be authorized)
    returns: decrypted field value (if requester has access)

/v3/data/vault/grants
    params: account
    returns: who has access to which fields
```

#### WebSocket Subscriptions (v3)

```
/v3/stream/actions
    subscribe: account, contract, action_name (filters)
    pushes: matching actions in real-time

/v3/stream/transfers
    subscribe: account, token (filters)
    pushes: incoming/outgoing transfers in real-time

/v3/stream/blocks
    subscribe: (no filter, all blocks)
    pushes: new blocks with finality status
```

## Architecture

### Integrated Plugin

```
cored binary
├── chain_plugin              — block production, consensus (existing)
├── chain_api_plugin          — /v1/chain/* endpoints (existing)
├── producer_plugin           — block production (existing)
├── net_plugin                — P2P networking (existing)
├── state_history_plugin      — raw data stream (existing, used internally)
│
├── index_plugin (NEW)        — indexer core
│   ├── consumes state_history internally (no WebSocket hop)
│   ├── writes to embedded database (PostgreSQL or SQLite)
│   ├── indexes: actions, transfers, accounts, gas, identity
│   └── full-text search index
│
└── index_api_plugin (NEW)    — /v3/* API endpoints
    ├── REST API server
    ├── WebSocket subscription server
    ├── query engine over indexed data
    └── rate limiting + pagination
```

### Node Types

Operators choose their node's role at startup:

```
# Pure validator (no indexing overhead)
cored --plugin chain_plugin --plugin producer_plugin --plugin net_plugin

# API node with v1 only (EOSIO compatible, minimal resources)
cored --plugin chain_plugin --plugin chain_api_plugin --plugin net_plugin

# Full index-API node (v1 + v3, rich queries, history, search)
cored --plugin chain_plugin --plugin chain_api_plugin \
      --plugin index_plugin --plugin index_api_plugin \
      --plugin net_plugin
```

**Validators don't index.** Only API nodes carry the indexing overhead.
This is opt-in, not forced on every node.

### Database

**Recommended: Embedded PostgreSQL or SQLite**

| Option | Pros | Cons |
|---|---|---|
| **SQLite** | Zero ops, embedded, no separate process | Single-writer, limited concurrency |
| **PostgreSQL** | Full SQL, concurrent reads, mature | Separate process (but much lighter than Elasticsearch) |
| **Embedded RocksDB** | Fast writes, LSM tree | No SQL, custom query layer needed |

**Recommendation:** SQLite for testnet (zero setup). PostgreSQL option for
production API nodes that need concurrent read scaling. Both supported via
a storage abstraction layer.

### Data Flow

```
Block produced/validated
    ↓
state_history_plugin captures traces + deltas
    ↓
index_plugin consumes internally (shared memory, no IPC)
    ↓
Parse and transform:
  - Extract actions → index by account, contract, action name
  - Extract transfers → index by sender, receiver, token, amount
  - Extract gas payments → index by payer, amount, block
  - Extract identity events → index by account, attestor, layer
  - Extract account changes → index by account, event type
    ↓
Write to database (batch, async)
    ↓
index_api_plugin serves /v3/* queries from database
```

### Performance Considerations

**Indexing overhead on API node:**
- Additional CPU: ~10-20% (parsing + writing)
- Additional storage: ~2-5x raw chain data (indexes + search)
- Additional memory: ~500MB-2GB for caches + write buffers

**Compared to Hyperion:**
- Hyperion + Elasticsearch: 64GB+ RAM, 3+ VMs, Terabytes of storage
- Index plugin: ~2-4GB additional RAM, single process, proportional storage
- **~10-20x reduction in infrastructure requirements**

## Implementation

### Phase 1: Core Indexer (Testnet T3-T4)

| Component | Effort |
|---|---|
| `index_plugin` skeleton (plugin lifecycle, state history consumption) | 1 week |
| Database abstraction layer (SQLite + PostgreSQL) | 1 week |
| Action indexer (by account, contract, action name) | 1 week |
| Transfer indexer (token transfers with amount, memo) | 1 week |
| Account event indexer (creation, permission changes) | 1 week |
| `/v3/history/*` endpoints | 1 week |
| `/v3/tokens/*` endpoints | 1 week |
| `/v3/accounts/*` endpoints | 3 days |
| Testing + integration | 1-2 weeks |
| **Subtotal** | **~8-10 weeks** |

### Phase 2: Gas + Identity Indexing (With Feature Launch)

| Component | Effort |
|---|---|
| Gas payment indexer | 3 days |
| `/v3/gas/*` endpoints | 3 days |
| Identity event indexer | 1 week |
| `/v3/identity/*` endpoints | 1 week |
| **Subtotal** | **~3-4 weeks** |

### Phase 3: Search + Subscriptions (Post-Launch)

| Component | Effort |
|---|---|
| Full-text search index (FTS5 for SQLite or tsvector for PostgreSQL) | 1-2 weeks |
| `/v3/search/*` endpoints | 1 week |
| WebSocket subscription server | 1-2 weeks |
| `/v3/stream/*` endpoints | 1 week |
| **Subtotal** | **~4-6 weeks** |

### Total: ~4-5 months across all phases

## Block Explorer

Built on `/v3/*` endpoints. The index-API node IS the backend.

```
Explorer (React/Next.js)
    ↓
/v3/history/*     → transaction/action views
/v3/tokens/*      → token balances, transfer history
/v3/accounts/*    → account details, resources, deposits
/v3/identity/*    → identity status, attestations
/v3/gas/*         → gas usage, pricing
/v3/search/*      → global search
/v1/chain/*       → real-time chain state
```

**Effort:** 3-5 weeks for a full-featured explorer on top of the v3 API.

## Competitive Advantage

No other EOSIO-family chain has a built-in indexer. No other L1 makes
full-history queries a one-binary deployment.

| Chain | Indexing | Deployment |
|---|---|---|
| **Anvo** | Built-in plugin | One binary |
| EOS/Telos/WAX | Hyperion (external) | nodeos + Elasticsearch + RabbitMQ + Redis + Node.js |
| Ethereum | External (Etherscan, Alchemy) | geth + separate indexer |
| Solana | Built into RPC (partial) | solana-validator (partial history) |
| Cosmos | External (various) | Node + separate indexer |

Solana is the closest comparison — their RPC node has some built-in history.
But it's limited and most production use relies on external providers.
Anvo would have comprehensive, purpose-built indexing as a first-class feature.
