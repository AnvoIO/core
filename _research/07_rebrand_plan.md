# Rebrand Plan: Removing EOSIO/Antelope/Spring/Leap Branding

## Scope

Remove all references to:
- **eosio** / **EOSIO** / **EOS** (original project)
- **antelope** / **AntelopeIO** / **Antelope** (successor project)
- **spring** / **Spring** (current project name)
- **leap** / **Leap** (predecessor to Spring)
- **block.one** / **Block.one** (original company)
- **nodeos** / **cleos** / **keosd** (executable names)

Replace with the new project identity: **Anvo Network** (`anvo` namespace, `ANVO_` macro prefix, `core` system accounts, `cored`/`core-cli`/`core-keys`/`core-util` executables).

## Scale of the Effort

### Total Counts by Term

| Term | Occurrences | Files | Directories |
|------|------------|-------|-------------|
| **eosio** (all forms) | ~7,693 | ~500+ | 27 |
| **nodeos** | ~1,939 | ~70 | 2 |
| **cleos** | ~560 | ~40 | 2 |
| **spring** | ~314 | ~30 | 2 |
| **antelope** | ~271 | ~30 | 0 |
| **keosd** | ~186 | ~20 | 2 |
| **eos_vm / EOS_VM** | ~70 | ~15 | 0 |
| **leap** | ~44 | ~10 | 0 |
| **block.one** | ~2 | 2 | 0 |
| **TOTAL** | **~11,079** | | **35** |

This is NOT a weekend project. It's a systematic, multi-week effort that must be
done carefully to avoid breaking the build, tests, and protocol compatibility.

### What Does NOT Need Renaming

- **`fc::` namespace** (~3,200 occurrences) — generic foundation library, not branded
- **WASM bytecode** — compiled contracts don't contain namespace strings
- **On-chain account names** (e.g., the `eosio` system account) — these are protocol-level
  names baked into existing chain state. Renaming them breaks compatibility. See
  "Protocol Account Names" section below.

## Renaming Categories

### Category 1: C++ Namespaces (HIGH EFFORT)

**`namespace eosio`** — 377 declarations across 366 files.

This is the primary namespace for the entire chain library, all plugins, and all programs.

```cpp
// Current
namespace eosio { namespace chain { ... } }
namespace eosio { namespace chain_apis { ... } }

// New
namespace anvo { namespace chain { ... } }
namespace anvo { namespace chain_apis { ... } }
```

**Strategy:** Global find-and-replace with verification.

- `namespace eosio` → `namespace anvo`
- `eosio::` → `anvo::` (qualified name references)
- `using namespace eosio` → `using namespace anvo`
- `EOSIO_` macro prefix → `ANVO_` (307 occurrences across 33 files)

**Risk:** Low if done mechanically. The compiler will catch any missed references.

### Category 2: Include Paths & Directory Structure (HIGH EFFORT)

**27 directories named `eosio`**, primarily under:
```
libraries/chain/include/eosio/           (121 header files)
libraries/state_history/include/eosio/
libraries/testing/include/eosio/
plugins/*/include/eosio/
```

**232 `#include` directives** referencing `eosio/`:
```cpp
// Current
#include <eosio/chain/controller.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

// New
#include <anvo/chain/controller.hpp>
#include <anvo/chain_plugin/chain_plugin.hpp>
```

**Strategy:**
1. Rename all `include/eosio/` directories to `include/anvo/`
2. Global find-and-replace on all `#include` directives
3. Update CMakeLists.txt include path declarations

**Risk:** Medium. Must update every include and every CMake target_include_directories.
The compiler catches all misses immediately.

### Category 3: Executable Names (MEDIUM EFFORT)

| Current | New (example) | Defined In |
|---------|---------------|-----------|
| `nodeos` | `cored` | `CMakeLists.txt` line 34 |
| `cleos` | `core-cli` | `CMakeLists.txt` line 33 |
| `keosd` | `core-keys` | `CMakeLists.txt` line 35 |
| `spring-util` | `core-util` | `CMakeLists.txt` line 36 |

These are defined as CMake variables:
```cmake
set( CLI_CLIENT_EXECUTABLE_NAME cleos )
set( NODE_EXECUTABLE_NAME nodeos )
set( KEY_STORE_EXECUTABLE_NAME keosd )
set( SPRING_UTIL_EXECUTABLE_NAME spring-util )
```

**Downstream references:**
- ~1,939 `nodeos` references (test scripts, docs, CLI help text)
- ~560 `cleos` references (docs, test scripts, bash completion)
- ~186 `keosd` references (docs, test scripts)
- 25 test files with `nodeos_` in the filename
- Program directories: `programs/nodeos/`, `programs/cleos/`, `programs/keosd/`
- Doc directories: `docs/01_nodeos/`, `docs/02_cleos/`, `docs/03_keosd/`

**Strategy:** Rename CMake variables first, then fix all downstream references.
Most test scripts reference executables through variables, so many will update
automatically. File and directory renames must be done manually.

### Category 4: CMake & Build System (MEDIUM EFFORT)

**Files to update:**
- Root `CMakeLists.txt` — project name: `project( antelope-spring )`
- `CMakeModules/eosio-config.cmake.in`
- `CMakeModules/leap-config.cmake.in`
- `CMakeModules/spring-config.cmake.in`
- `CMakeModules/EosioTester.cmake.in`
- `CMakeModules/EosioTesterBuild.cmake.in`
- `eosio.version.in` (version namespace)
- `eos.doxygen.in` (Doxygen config)
- `package.cmake` (package naming)

**CMake target names:**
All library and executable targets use EOSIO-derived names. These need updating in
CMakeLists.txt files throughout the tree and in any `target_link_libraries` references.

### Category 5: WASM Runtime Names (MEDIUM EFFORT)

~70 occurrences of `eos_vm` / `EOS_VM`:

| Current | New |
|---------|-----|
| `eos-vm` (runtime name) | Keep `eos-vm` (internal, not user-facing) |
| `eos-vm-jit` | Keep (internal) |
| `eos-vm-oc` | Keep (internal) |
| `EOS_VM_OC_*` macros | Keep (internal) |
| `eosvmoc_*` identifiers | `newvmoc_*` or keep |

**Decision point:** The eos-vm library (`libraries/eos-vm/`) is an external submodule.
You have two options:
1. **Fork and rename eos-vm** — clean but means maintaining a fork of eos-vm
2. **Keep eos-vm naming internally** — pragmatic, less maintenance, the name is
   internal-only and not user-facing

**Recommendation:** Keep the eos-vm naming internally for now. It's an implementation
detail. Users never see it. Rename only user-facing references (config flags like
`--wasm-runtime eos-vm-oc` → rename user-facing config flags).

### Category 6: System Account Names (CRITICAL — DO NOT RENAME)

~698 occurrences of `eosio` as a system account name:

```cpp
name("eosio")
name("eosio.token")
name("eosio.system")
name("eosio.msig")
name("eosio.wrap")
name("eosio.bios")
```

**These MUST NOT be changed in the node software.** They are protocol-level account
names embedded in:
- The genesis state
- System contract ABIs
- Every existing chain's on-chain state
- SDKs and tools across the ecosystem

Changing them would break compatibility with existing EOSIO chains — the exact
opposite of our goal.

**However**, for a fresh new chain (not migrating existing state), you could:
1. Deploy system contracts to new account names in genesis
2. Add aliases or configuration options for system account names
3. Keep the `eosio.*` names as defaults for backward compatibility

**Recommendation:** Keep `eosio.*` account names as protocol constants. They're
internal to chain state, not user-facing branding. A future protocol upgrade
could add configurable system account names if desired.

### Category 7: Protocol Features & ABI Types (CAREFUL)

Some protocol feature names and ABI type strings contain "eosio":
- `eosio::abi` type identifiers in ABI serialization
- Protocol feature names in activation records

**These are consensus-level identifiers.** Changing them creates a fork-incompatible
chain. Keep them unchanged for compatibility.

### Category 8: Documentation & Comments (~2,500+ occurrences)

- README.md
- docs/ directory (1,500+ occurrences across 01_nodeos/, 02_cleos/, 03_keosd/)
- Code comments referencing EOSIO, Antelope, Spring
- License headers

**Strategy:** Bulk find-and-replace. Low risk — doesn't affect compilation or runtime.

### Category 9: Test Scripts (~850+ occurrences)

- 25 files named `nodeos_*.py`
- Tests reference executables by name
- Test harness classes named after executables

**Strategy:** Rename files, update references. Most test frameworks use variables
for executable paths, so internal references update via CMake. String literals in
test assertions/logs need manual review.

### Category 10: CI/CD & GitHub (SOFT)

- `.github/workflows/*.yaml` — runner labels, artifact names
- GitHub URLs pointing to `AntelopeIO/spring`
- Docker image names
- Package naming (`*-amd64.deb`)

**Strategy:** Update after code changes are complete.

## Execution Plan

### Phase A: Preparation (1-2 days)

1. **Choose new names** for:
   - Project name (replaces "spring" / "antelope-spring")
   - Node daemon (replaces "nodeos")
   - CLI client (replaces "cleos")
   - Key daemon (replaces "keosd")
   - Utility tool (replaces "spring-util")
   - C++ namespace (replaces `eosio`)
   - Macro prefix (replaces `EOSIO_`)

2. **Create a renaming script** that handles:
   - Directory renames
   - File renames
   - Content replacement with proper case handling
   - Git history preservation (use `git mv` for directories/files)

3. **Set up a comprehensive build verification**:
   - Full build on x86_64
   - Full unit test suite run
   - Full integration test suite run
   - Compare test results before/after rename

### Phase B: Structural Renames (1-2 days)

**Order matters.** Do these first as they affect file paths:

1. Rename directories:
   - `include/eosio/` → `include/anvo/` (27 directories)
   - `programs/nodeos/` → `programs/cored/`
   - `programs/cleos/` → `programs/core-cli/`
   - `programs/keosd/` → `programs/core-keys/`
   - `programs/spring-util/` → `programs/core-util/`
   - `docs/01_nodeos/` → `docs/01_cored/`
   - `docs/02_cleos/` → `docs/02_core-cli/`
   - `docs/03_keosd/` → `docs/03_core-keys/`

2. Rename files:
   - `eosio.version.in` → `anvo.version.in`
   - `eos.doxygen.in` → `anvo.doxygen.in`
   - `CMakeModules/eosio-config.cmake.in` → `CMakeModules/anvo-config.cmake.in`
   - `CMakeModules/EosioTester*.cmake.in` → `CMakeModules/AnvoTester*.cmake.in`
   - 25 `nodeos_*.py` test files → `cored_*.py`

3. **Build and verify** — expect failures, fix include path issues.

### Phase C: Content Replacement (2-3 days)

Apply in this order to minimize conflicts:

1. **Namespace replacement** (biggest impact):
   - `namespace eosio` → `namespace anvo`
   - `eosio::` → `anvo::`
   - `using namespace eosio` → `using namespace anvo`

2. **Include path replacement**:
   - `#include <eosio/` → `#include <anvo/`
   - `#include "eosio/` → `#include "anvo/`

3. **Macro prefix replacement**:
   - `EOSIO_` → `ANVO_`

4. **Executable name replacement**:
   - `nodeos` → new node name (in CMake, scripts, docs)
   - `cleos` → new CLI name
   - `keosd` → new key daemon name
   - `spring-util` → new utility name
   - `spring` → new project name (in CMake, docs, version strings)

5. **Organization/project name replacement**:
   - `AntelopeIO` → new org name (in URLs, comments)
   - `antelope-spring` → new project name (in CMake project declaration)
   - `Antelope` → new project name (in docs, comments)
   - `leap` → new project name (in comments, cmake configs)

6. **Build and verify** — full build + full test suite.

### Phase D: Protocol-Safe Verification (1 day)

**Critical step.** Verify that protocol-level identifiers were NOT changed:

1. System account names still use `eosio.*`
2. ABI type identifiers unchanged
3. Protocol feature names unchanged
4. Genesis configuration compatible
5. Snapshot import/export still works
6. All unit tests pass with identical results

### Phase E: Cleanup (1-2 days)

1. Update LICENSE with new copyright holder
2. Update README.md
3. Update all documentation
4. Update CI/CD workflows
5. Update package naming
6. Final full build + test verification

## What NOT to Rename

| Item | Reason |
|------|--------|
| `eosio` system account names | Protocol compatibility — breaks existing chains |
| `eosio.*` contract names in genesis | Same — protocol level |
| ABI type strings containing "eosio" | Consensus-level serialization format |
| Protocol feature activation names | Consensus compatibility |
| `fc::` namespace | Generic foundation library, not branded |
| `eos-vm` internal library (optional) | Implementation detail, not user-facing |

## Automation

A well-written rename script can handle ~90% of this automatically:

```bash
#!/bin/bash
# Pseudocode — actual script would need careful regex handling

OLD_NS="eosio"
NEW_NS="anvo"
OLD_NODE="nodeos"
NEW_NODE="cored"
# ... etc

# Step 1: Directory renames (use git mv)
find . -type d -name "$OLD_NS" | while read dir; do
    git mv "$dir" "$(dirname $dir)/$NEW_NS"
done

# Step 2: File renames
find . -type f -name "*${OLD_NS}*" | while read f; do
    git mv "$f" "$(echo $f | sed "s/$OLD_NS/$NEW_NS/g")"
done

# Step 3: Content replacement (EXCLUDE protocol constants)
find . -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \
    -o -name "*.cmake" -o -name "*.cmake.in" -o -name "*.py" \
    -o -name "*.md" -o -name "*.yaml" -o -name "*.in" \) | while read f; do
    sed -i "s/namespace eosio/namespace $NEW_NS/g" "$f"
    sed -i "s/eosio::/anvo::/g" "$f"
    # ... more patterns
done
```

The remaining ~10% requires manual review:
- Protocol account name constants (must be preserved)
- String literals that serve as identifiers vs. branding
- Test assertions that check specific output strings
- Ambiguous uses of "eos" that might be part of other words

## Timeline

| Phase | Duration | Dependencies |
|-------|----------|-------------|
| A. Preparation | 1-2 days | Choose names |
| B. Structural renames | 1-2 days | Phase A |
| C. Content replacement | 2-3 days | Phase B |
| D. Protocol verification | 1 day | Phase C |
| E. Cleanup | 1-2 days | Phase D |
| **Total** | **~1-2 weeks** | |

**Recommendation:** Do this as the very first step after forking (Phase 1A in the
fork plan), before any feature work begins. Rebranding after feature branches diverge
creates merge conflicts that compound over time.
