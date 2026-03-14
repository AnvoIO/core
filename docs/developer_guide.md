# Developer Guide

Reference for C++ developers working on the Anvo Core node codebase.

## Namespace

All code lives under `core_net::` (was `eosio::`):

```cpp
#include <core_net/chain/controller.hpp>
#include <core_net/chain/config.hpp>

namespace core_net::chain { ... }
```

## Include Paths

```cpp
// Old
#include <eosio/chain/controller.hpp>

// New
#include <core_net/chain/controller.hpp>
```

## Macro Prefix

| Old | New |
|-----|-----|
| `EOSIO_*` | `CORE_NET_*` |
| `EOS_VM_*` | `CORE_NET_VM_*` |

Examples: `CORE_NET_ASSERT`, `CORE_NET_THROW`, `CORE_NET_VM_OC_ENABLE`.

## System Account Config

System account names are no longer static constants -- they are accessor functions:

```cpp
// Old: static constant
if (account == config::system_account_name) { ... }

// New: function call
if (account == config::system_account_name()) { ... }
```

All accessors follow this pattern:

| Function | Returns |
|----------|---------|
| `config::system_account_name()` | System account name |
| `config::null_account_name()` | Null account name |
| `config::producers_account_name()` | Producers account name |
| `config::auth_scope()` | Auth scope name |
| `config::all_scope()` | All scope name |
| `config::any_name()` | Any name |
| `config::code_name()` | Code permission name |
| `config::system_account_prefix_str()` | System account prefix string |

## CMake Targets

| Old | New |
|-----|-----|
| `eosio_chain` | `core_net_chain` |
| `eosio_testing` | `core_net_testing` |

Project name: `anvo-core`

## Build

```bash
mkdir build && cd build
cmake -DENABLE_OC=OFF -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Binaries are output to `build/bin/`. Key targets: `core_netd`, `core-cli`, `core-wallet`, `core-util`.

## Testing

```bash
cd build
ctest -j$(nproc) --output-on-failure
```

Link against `core_net_testing` in your test CMakeLists for chain test fixtures.
