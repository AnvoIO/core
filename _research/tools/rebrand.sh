#!/bin/bash
# =============================================================================
# Anvo Network Rebrand Script
# =============================================================================
#
# Renames EOSIO/Spring/Antelope branding to Anvo Network naming:
#   - Namespace: eosio:: → cored::
#   - Includes:  include/eosio/ → include/cored/
#   - Macros:    EOSIO_ → CORE_
#   - Executables: nodeos → cored, cleos → core-cli, keosd → core-keys,
#                  spring-util → core-util
#   - CMake project: antelope-spring → anvo-core
#
# IMPORTANT: This script must be run from the repository root.
# IMPORTANT: All submodules must be initialized before running.
# IMPORTANT: Run on a clean working tree (commit or stash changes first).
#
# Excludes:
#   - libraries/eos-vm/ (external submodule, keeps eosio naming)
#   - libraries/boost/ (external submodule)
#   - unittests/contracts/ and libraries/testing/contracts/ (protocol-level)
#   - plugins/trace_api_plugin/examples/ (protocol-level ABIs)
#   - _research/ (planning docs, not source code)
#   - .git/ and build/
#
# Submodule namespaces preserved:
#   - eosio::vm:: (eos-vm library)
#   - eosio_rapidjson (rapidjson library)
#
# Lessons learned:
#   - NEVER use sed 's/core/cored/g' — it replaces core inside identifiers
#     like finality_core, _core_instance, etc.
#   - Use perl negative lookbehind: (?<![a-zA-Z_])core:: → cored::
#   - keosd appears as C++ identifier prefix (_keosd_*) — only replace in
#     string literals, not in identifiers
#   - eosio/vm/ includes and eosio::vm:: namespace must be fully qualified
#     after rename (was previously resolved via namespace eosio {})
#   - boost::core is a real namespace — core:: as our namespace collides
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

# Exclusion patterns for find commands
EXCLUDES=(
    ! -path "./.git/*"
    ! -path "./build/*"
    ! -path "./libraries/eos-vm/*"
    ! -path "./libraries/boost/*"
    ! -path "./_research/*"
)

EXCLUDES_STRICT=(
    "${EXCLUDES[@]}"
    ! -path "./unittests/contracts/*"
    ! -path "./libraries/testing/contracts/*"
    ! -path "./plugins/trace_api_plugin/examples/*"
)

SOURCE_FILES=( -name "*.hpp" -o -name "*.cpp" -o -name "*.h" -o -name "*.in" )
CMAKE_FILES=( -name "CMakeLists.txt" -o -name "*.cmake" -o -name "*.cmake.in" )
PYTHON_FILES=( -name "*.py" )

echo "============================================="
echo "Anvo Network Rebrand — Starting"
echo "============================================="

# ─────────────────────────────────────────────────
# PHASE 1: Directory renames
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 1: Directory renames ---"

# Rename include/eosio/ → include/cored/ (27 directories)
find . -type d -name "eosio" -path "*/include/*" \
    "${EXCLUDES[@]}" \
    -exec bash -c 'git mv "$1" "$(dirname "$1")/cored"' _ {} \;
echo "  Include directories renamed: eosio → cored"

# Rename program directories
git mv programs/nodeos programs/cored
git mv programs/cleos programs/core-cli
git mv programs/keosd programs/core-keys
git mv programs/spring-util programs/core-util
echo "  Program directories renamed"

# ─────────────────────────────────────────────────
# PHASE 2: File renames (generic names)
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 2: File renames ---"

# Build/infra files
git mv CMakeModules/eosio-config.cmake.in CMakeModules/cored-config.cmake.in
git mv CMakeModules/leap-config.cmake.in CMakeModules/leap-config.cmake.in.legacy
git mv CMakeModules/spring-config.cmake.in CMakeModules/spring-config.cmake.in.legacy
git mv eosio.version.in cored.version.in
git mv libraries/cli11/bash-completion/completions/spring-util libraries/cli11/bash-completion/completions/core-util
git mv libraries/cli11/bash-completion/completions/cleos libraries/cli11/bash-completion/completions/core-cli
git mv plugins/eosio-make_new_plugin.sh plugins/make_new_plugin.sh
echo "  Build/infra files renamed"

# Chain library files → generic names
git mv libraries/chain/eosio_contract.cpp libraries/chain/system_contract.cpp
git mv libraries/chain/eosio_contract_abi.cpp libraries/chain/system_contract_abi.cpp
git mv libraries/chain/eosio_contract_abi_bin.cpp libraries/chain/system_contract_abi_bin.cpp
git mv libraries/chain/include/cored/chain/eosio_contract.hpp libraries/chain/include/cored/chain/system_contract.hpp
git mv libraries/chain/include/cored/chain/wasm_eosio_binary_ops.hpp libraries/chain/include/cored/chain/wasm_binary_ops.hpp
git mv libraries/chain/include/cored/chain/wasm_eosio_constraints.hpp libraries/chain/include/cored/chain/wasm_constraints.hpp
git mv libraries/chain/include/cored/chain/wasm_eosio_injection.hpp libraries/chain/include/cored/chain/wasm_injection.hpp
git mv libraries/chain/include/cored/chain/wasm_eosio_validation.hpp libraries/chain/include/cored/chain/wasm_validation.hpp
git mv libraries/chain/wasm_eosio_binary_ops.cpp libraries/chain/wasm_binary_ops.cpp
git mv libraries/chain/wasm_eosio_injection.cpp libraries/chain/wasm_injection.cpp
git mv libraries/chain/wasm_eosio_validation.cpp libraries/chain/wasm_validation.cpp
echo "  Chain library files renamed"

# Test files
git mv unittests/eosio.token_tests.cpp unittests/token_tests.cpp
git mv unittests/eosio_system_tester.hpp unittests/system_tester.hpp
git mv unittests/wasm-spec-tests/generator/eosio_test_generator.cpp unittests/wasm-spec-tests/generator/wasm_test_generator.cpp
git mv unittests/wasm-spec-tests/generator/eosio_test_generator.hpp unittests/wasm-spec-tests/generator/wasm_test_generator.hpp
git mv unittests/wasm-spec-tests/generator/eosio_wasm_spec_test_generator.cpp unittests/wasm-spec-tests/generator/wasm_spec_test_generator.cpp
git mv unittests/wasm-spec-tests/generator/generate_eosio_tests.py unittests/wasm-spec-tests/generator/generate_wasm_tests.py
git mv unittests/wasm-spec-tests/generator/setup_eosio_tests.py unittests/wasm-spec-tests/generator/setup_wasm_tests.py
git mv tests/spring_util_bls_test.py tests/core_util_bls_test.py
git mv tests/spring_util_snapshot_info_test.py tests/core_util_snapshot_info_test.py
git mv tests/keosd_auto_launch_test.py tests/core-keys_auto_launch_test.py
echo "  Test files renamed"

# Rename nodeos_*.py test files → cored_*.py
for f in tests/nodeos_*.py; do
    [ -f "$f" ] && git mv "$f" "tests/cored_$(basename "$f" | sed 's/^nodeos_//')"
done
git mv tests/nodeos_late_block_test_shape.json tests/cored_late_block_test_shape.json 2>/dev/null || true
git mv tests/PerformanceHarness/NodeosPluginArgs tests/PerformanceHarness/CorePluginArgs
echo "  Test file names updated"

# Doc files
git mv "docs/02_cleos/02_how-to-guides/how-to-transfer-an-eosio.token-token.md" \
       "docs/02_cleos/02_how-to-guides/how-to-transfer-a-token.md"
git mv docs/spring_components.png docs/components.png
echo "  Doc files renamed"

# ─────────────────────────────────────────────────
# PHASE 3: Namespace replacement (eosio:: → cored::)
# Uses perl negative lookbehind to avoid replacing
# 'eosio' inside identifiers like 'apply_eosio_newaccount'
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 3: Namespace replacement ---"

find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/(?<![a-zA-Z_])eosio::/cored::/g;
        s/\bnamespace eosio\b/namespace cored/g;
        s/\busing namespace eosio\b/using namespace cored/g;
    ' {} +
echo "  Namespace eosio → cored: done"

# ─────────────────────────────────────────────────
# PHASE 4: Include paths (eosio/ → cored/)
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 4: Include paths ---"

find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i \
        -e 's|#include <eosio/|#include <cored/|g' \
        -e 's|#include "eosio/|#include "cored/|g' \
        -e 's|#include<eosio/|#include<cored/|g' \
    {} +
echo "  Include paths eosio/ → cored/: done"

# Fix renamed file includes
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES_STRICT[@]}" \
    -exec sed -i \
        -e 's|wasm_eosio_constraints\.hpp|wasm_constraints.hpp|g' \
        -e 's|wasm_eosio_binary_ops\.hpp|wasm_binary_ops.hpp|g' \
        -e 's|wasm_eosio_injection\.hpp|wasm_injection.hpp|g' \
        -e 's|wasm_eosio_validation\.hpp|wasm_validation.hpp|g' \
        -e 's|eosio_contract\.hpp|system_contract.hpp|g' \
        -e 's|"eosio_system_tester.hpp"|"system_tester.hpp"|g' \
        -e 's|<eosio_system_tester.hpp>|<system_tester.hpp>|g' \
    {} +
echo "  Renamed file includes: done"

# Fix wasm-jit relative paths
sed -i 's|include/eosio/chain/wasm_eosio_constraints\.hpp|include/cored/chain/wasm_constraints.hpp|g' \
    libraries/wasm-jit/Include/Inline/Serialization.h \
    libraries/wasm-jit/Source/WASM/WASMSerialization.cpp
echo "  wasm-jit relative paths: done"

# ─────────────────────────────────────────────────
# PHASE 5: Restore eos-vm submodule references
# The namespace rename (eosio:: → cored::) incorrectly
# catches eosio::vm:: and eosio/vm/ references.
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 5: Restore eos-vm submodule references ---"

find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i \
        -e 's|cored/vm/|eosio/vm/|g' \
        -e 's/cored::vm::/eosio::vm::/g' \
    {} +

# Fully qualify ALL unqualified vm:: references
# (these relied on being inside namespace eosio {})
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/(?<![a-zA-Z_:])vm::(wasm_allocator|wasm_ptr_t|wasm_size_t|wasm_code_ptr)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(span<)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(argument_proxy)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(is_argument_proxy)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(type_converter)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(execution_interface)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(registered_host_functions)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(dynamic_extent)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(interpreter)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(jit)\b/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(jit_profile)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(set_profile_interval)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(tag<)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(invoke_on)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(is_span_type_v)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(host_function)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(native_value)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(no_match_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(i32_const_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(i64_const_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(f32_const_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(f64_const_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(i64)\b/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(detail::)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(flatten_parameters_t)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(exception)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(func_type)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(profile_instr_map)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(backend)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(null_backend)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(guarded_vector)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(module)/eosio::vm::$1/g;
        s/(?<![a-zA-Z_:])vm::(growable_allocator)/eosio::vm::$1/g;
    ' {} +

# Clean up any double eosio::eosio::vm
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i 's/eosio::eosio::vm/eosio::vm/g' {} +

echo "  eos-vm references restored and fully qualified"

# ─────────────────────────────────────────────────
# PHASE 6: Restore other submodule namespaces
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 6: Restore submodule namespaces ---"

find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i 's/cored_rapidjson/eosio_rapidjson/g' {} +
echo "  eosio_rapidjson restored"

# ─────────────────────────────────────────────────
# PHASE 7: Macro prefix (EOSIO_ → CORE_)
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 7: Macro prefix ---"

find . -type f \( "${SOURCE_FILES[@]}" "${CMAKE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i 's/EOSIO_/CORE_/g' {} +
echo "  EOSIO_ → CORE_: done"

# ─────────────────────────────────────────────────
# PHASE 8: CMake project + executable names
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 8: CMake project and executables ---"

# Root CMakeLists.txt
sed -i 's/project( antelope-spring )/project( anvo-core )/' CMakeLists.txt
sed -i 's/set( CLI_CLIENT_EXECUTABLE_NAME cleos )/set( CLI_CLIENT_EXECUTABLE_NAME core-cli )/' CMakeLists.txt
sed -i 's/set( NODE_EXECUTABLE_NAME nodeos )/set( NODE_EXECUTABLE_NAME cored )/' CMakeLists.txt
sed -i 's/set( KEY_STORE_EXECUTABLE_NAME keosd )/set( KEY_STORE_EXECUTABLE_NAME core-keys )/' CMakeLists.txt
sed -i 's/set( SPRING_UTIL_EXECUTABLE_NAME spring-util )/set( CORE_UTIL_EXECUTABLE_NAME core-util )/' CMakeLists.txt

# All CMake files: subdirectory and path references
find . -type f \( "${CMAKE_FILES[@]}" \) "${EXCLUDES[@]}" ! -name "*.legacy" \
    -exec sed -i \
        -e 's/SPRING_UTIL_EXECUTABLE_NAME/CORE_UTIL_EXECUTABLE_NAME/g' \
        -e 's/add_subdirectory(nodeos)/add_subdirectory(cored)/g' \
        -e 's/add_subdirectory(cleos)/add_subdirectory(core-cli)/g' \
        -e 's/add_subdirectory(keosd)/add_subdirectory(core-keys)/g' \
        -e 's/add_subdirectory(spring-util)/add_subdirectory(core-util)/g' \
        -e 's/add_subdirectory( spring-util )/add_subdirectory( core-util )/g' \
        -e 's/antelope-spring/anvo-core/g' \
        -e 's|include/eosio|include/cored|g' \
        -e 's|INCLUDEDIR}/eosio|INCLUDEDIR}/cored|g' \
        -e 's/eosio_chain/cored_chain/g' \
        -e 's/eosio_testing/cored_testing/g' \
        -e 's/NodeosPluginArgs/CorePluginArgs/g' \
    {} +

# Fix version and config file references
sed -i 's|eosio.version.in|cored.version.in|g' CMakeLists.txt
sed -i 's|eosio.version.hpp|cored.version.hpp|g' CMakeLists.txt
sed -i 's|eosio-config|cored-config|g' CMakeLists.txt
sed -i 's|spring-config|cored-config|g' CMakeLists.txt
sed -i 's|leap-config|cored-config|g' CMakeLists.txt
sed -i 's|cmake/eosio|cmake/cored|g' CMakeLists.txt
sed -i 's|cmake/leap|cmake/cored|g' CMakeLists.txt
sed -i 's|cmake/spring|cmake/cored|g' CMakeLists.txt
sed -i 's|licenses/spring|licenses/core|g' CMakeLists.txt
sed -i 's|spring_testing|core_testing|g' CMakeLists.txt

# Remove legacy config install targets (eosio/leap configs no longer exist)
# TODO: Clean up CMakeLists.txt to remove duplicate configure_file calls

# Fix test CMake references
sed -i 's/spring_util_bls_test/core_util_bls_test/g' tests/CMakeLists.txt
sed -i 's/spring_util_snapshot_info_test/core_util_snapshot_info_test/g' tests/CMakeLists.txt

echo "  CMake project and executable names: done"

# ─────────────────────────────────────────────────
# PHASE 8b: Test code identifier renames
# Class names, WASM/ABI accessor functions, and
# function names that use eosio in their identifiers
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 8b: Test code identifier renames ---"

# Rename C++ test class/struct identifiers
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES_STRICT[@]}" \
    -exec perl -pi -e '
        s/\beosio_system_tester\b/cored_system_tester/g;
        s/\beosio_system_testers\b/cored_system_testers/g;
        s/\beosio_token_tester\b/cored_token_tester/g;
        s/\beosio_token_testers\b/cored_token_testers/g;
        s/\beosio_token_tests\b/cored_token_tests/g;
    ' {} +
echo "  Test class identifiers renamed"

# Rename WASM/ABI accessor function names in .hpp.in / .cpp.in templates
# Only the C++ function name (first arg to MAKE_*_WASM_ABI) changes;
# the on-chain contract name (second arg, e.g. eosio.bios) stays.
find . -type f \( -name "*.hpp.in" -o -name "*.cpp.in" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/\beosio_bios\b/cored_bios/g;
        s/\bbefore_producer_authority_eosio_bios\b/before_producer_authority_cored_bios/g;
        s/\bbefore_preactivate_eosio_bios\b/before_preactivate_cored_bios/g;
        s/\beosio_msig\b/cored_msig/g;
        s/\beosio_system\b/cored_system/g;
        s/\beosio_token\b/cored_token/g;
        s/\beosio_wrap\b/cored_wrap/g;
        s/\beosio_testing_contract_\b/cored_testing_contract_/g;
        s/\bgeosio_testing_contract_\b/gcored_testing_contract_/g;
    ' {} +
echo "  WASM/ABI templates renamed"

# Rename WASM/ABI accessor calls in C++ source files
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES_STRICT[@]}" \
    -exec perl -pi -e '
        s/contracts::eosio_bios_wasm/contracts::cored_bios_wasm/g;
        s/contracts::eosio_bios_abi/contracts::cored_bios_abi/g;
        s/contracts::before_preactivate_eosio_bios_wasm/contracts::before_preactivate_cored_bios_wasm/g;
        s/contracts::before_preactivate_eosio_bios_abi/contracts::before_preactivate_cored_bios_abi/g;
        s/contracts::before_producer_authority_eosio_bios_wasm/contracts::before_producer_authority_cored_bios_wasm/g;
        s/contracts::before_producer_authority_eosio_bios_abi/contracts::before_producer_authority_cored_bios_abi/g;
        s/test_contracts::eosio_token_wasm/test_contracts::cored_token_wasm/g;
        s/test_contracts::eosio_token_abi/test_contracts::cored_token_abi/g;
        s/test_contracts::eosio_msig_wasm/test_contracts::cored_msig_wasm/g;
        s/test_contracts::eosio_msig_abi/test_contracts::cored_msig_abi/g;
        s/test_contracts::eosio_system_wasm/test_contracts::cored_system_wasm/g;
        s/test_contracts::eosio_system_abi/test_contracts::cored_system_abi/g;
        s/test_contracts::eosio_wrap_wasm/test_contracts::cored_wrap_wasm/g;
        s/test_contracts::eosio_wrap_abi/test_contracts::cored_wrap_abi/g;
    ' {} +
echo "  WASM/ABI accessor calls renamed"

# Rename eosio_contract_abi function
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/\beosio_contract_abi\b/cored_contract_abi/g;
        s/\beosio_system_abi\b/cored_system_abi/g;
    ' {} +
echo "  eosio_contract_abi → cored_contract_abi: done"

# Rename remaining C++ identifiers with eosio prefix
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/\beosio_root_key\b/core_root_key/g;
        s/\beosio_injected_module_name\b/core_injected_module_name/g;
    ' {} +
echo "  eosio_root_key, eosio_injected_module_name renamed"

# Rename eosio_system namespace (test harness namespace, not the on-chain eosio.system)
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES_STRICT[@]}" \
    -exec perl -pi -e '
        s/\bnamespace eosio_system\b/namespace cored_system/g;
        s/\busing namespace eosio_system\b/using namespace cored_system/g;
        s/\beosio_system::eosio_system_testers\b/cored_system::cored_system_testers/g;
    ' {} +
echo "  eosio_system namespace → cored_system: done"

# Rename plugin name strings in Python test harness and shell scripts
find . -type f \( "${PYTHON_FILES[@]}" -o -name "*.sh" \) "${EXCLUDES[@]}" \
    -exec perl -pi -e '
        s/eosio::([\w_]+_plugin)/cored::$1/g;
        s/eosio::([\w_]+_api)\b/cored::$1/g;
    ' {} +
echo "  Plugin name strings eosio:: → cored:: in test files: done"

# Fix config directory paths
find . -type f \( "${PYTHON_FILES[@]}" -o -name "*.sh" -o -name "*.md" \) "${EXCLUDES[@]}" \
    -exec sed -i 's|etc/eosio/|etc/cored/|g' {} +
sed -i "s|Path('etc') / 'eosio'|Path('etc') / 'cored'|g" tests/TestHarness/launcher.py
echo "  Config paths etc/eosio/ → etc/cored/: done"

# Rename generate_nodeos_plugin_args script
if [ -f tests/PerformanceHarness/CorePluginArgs/generate_nodeos_plugin_args_class_files.py ]; then
    git mv tests/PerformanceHarness/CorePluginArgs/generate_nodeos_plugin_args_class_files.py \
           tests/PerformanceHarness/CorePluginArgs/generate_cored_plugin_args_class_files.py
fi
# Update _pluginNamespace in the generator script
sed -i 's/_pluginNamespace: str=\\"eosio\\"/_pluginNamespace: str=\\"cored\\"/g' \
    tests/PerformanceHarness/CorePluginArgs/generate_cored_plugin_args_class_files.py
echo "  PerformanceHarness script renamed and updated"

# Fix trailing /// eosio comments in .hpp.in files
find . -type f \( -name "*.hpp.in" -o -name "*.cpp.in" \) "${EXCLUDES[@]}" \
    -exec sed -i 's|///  *eosio$|/// cored|g' {} +
echo "  Trailing namespace comments updated"

# ─────────────────────────────────────────────────
# PHASE 9: Executable name strings in source code
# nodeos/cleos/keosd in string literals and comments
# IMPORTANT: keosd stays as identifier prefix (_keosd_*)
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 9: Executable name strings ---"

# C++ source: replace in string literals only for keosd
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i \
        -e 's/"nodeos"/"cored"/g' \
        -e "s/'nodeos'/'cored'/g" \
        -e 's/"cleos"/"core-cli"/g' \
        -e "s/'cleos'/'core-cli'/g" \
        -e 's/"keosd"/"core-keys"/g' \
        -e "s/'keosd'/'core-keys'/g" \
        -e 's/"spring-util"/"core-util"/g' \
        -e "s/'spring-util'/'core-util'/g" \
        -e 's/"keosd-provider-timeout"/"core-keys-provider-timeout"/g' \
    {} +

# C++ source: replace nodeos/cleos broadly (safe — not used as identifiers)
find . -type f \( "${SOURCE_FILES[@]}" \) "${EXCLUDES_STRICT[@]}" \
    -exec sed -i \
        -e 's/nodeos/cored/g' \
        -e 's/cleos/core-cli/g' \
        -e 's/spring-util/core-util/g' \
        -e 's/spring_util/core_util/g' \
    {} +

# Python tests: replace all executable references
find . -type f \( "${PYTHON_FILES[@]}" \) "${EXCLUDES[@]}" \
    -exec sed -i \
        -e 's/nodeos/cored/g' \
        -e 's/Nodeos/Cored/g' \
        -e 's/"cleos"/"core-cli"/g' \
        -e "s/'cleos'/'core-cli'/g" \
        -e 's/ClientName="cleos"/ClientName="core-cli"/g' \
        -e 's/"keosd"/"core-keys"/g' \
        -e "s/'keosd'/'core-keys'/g" \
        -e 's/spring-util/core-util/g' \
        -e 's/spring_util/core_util/g' \
    {} +

# Python code generators that emit C++ (hardcoded eosio references in output)
sed -i "s|eosio/chain/protocol_feature_manager.hpp|cored/chain/protocol_feature_manager.hpp|g" \
    unittests/gen_protocol_feature_digest_tests.py
sed -i "s|using namespace eosio::chain|using namespace cored::chain|g" \
    unittests/gen_protocol_feature_digest_tests.py

echo "  Executable name strings: done"

# ─────────────────────────────────────────────────
# PHASE 10: Verification
# ─────────────────────────────────────────────────
echo ""
echo "--- Phase 10: Verification ---"

ISSUES=0

# Check for remaining namespace eosio (outside submodules)
COUNT=$(grep -rn "namespace eosio" --include="*.hpp" --include="*.cpp" --include="*.h" \
    | grep -v ".git" | grep -v "build" | grep -v "eos-vm" | grep -v "_research" | grep -v "boost" | wc -l)
echo "  namespace eosio remaining: $COUNT"
[ "$COUNT" -gt 0 ] && ISSUES=$((ISSUES + 1))

# Check for eosio:: (outside submodules, excluding eosio::vm)
COUNT=$(grep -rn "eosio::" --include="*.hpp" --include="*.cpp" --include="*.h" \
    | grep -v ".git" | grep -v "build" | grep -v "eos-vm" | grep -v "_research" | grep -v "boost" \
    | grep -v "eosio::vm" | wc -l)
echo "  eosio:: remaining (excl eosio::vm): $COUNT"

# Check for double eosio
COUNT=$(grep -rn "eosio::eosio" --include="*.cpp" --include="*.hpp" --include="*.h" \
    | grep -v ".git" | grep -v "build" | grep -v "eos-vm" | grep -v "_research" | wc -l)
echo "  eosio::eosio (double): $COUNT"
[ "$COUNT" -gt 0 ] && ISSUES=$((ISSUES + 1))

# Check for boost::cored (should be boost::core)
COUNT=$(grep -rn "boost::cored" --include="*.cpp" --include="*.hpp" \
    | grep -v ".git" | grep -v "build" | wc -l)
echo "  boost::cored (should be boost::core): $COUNT"
[ "$COUNT" -gt 0 ] && ISSUES=$((ISSUES + 1))

echo ""
if [ "$ISSUES" -gt 0 ]; then
    echo "⚠️  $ISSUES verification issues found. Review above."
else
    echo "✓ Verification passed."
fi

echo ""
echo "============================================="
echo "Rebrand complete. Next steps:"
echo "  1. rm -rf build && mkdir build && cd build"
echo "  2. cmake -DENABLE_OC=OFF -DCMAKE_BUILD_TYPE=Release .."
echo "  3. make -j\$(nproc)"
echo "  4. If build passes, commit."
echo "============================================="
