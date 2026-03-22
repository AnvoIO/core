set(APPBASE_INSTALL_COMPONENT "dev")
set(SOFTFLOAT_INSTALL_COMPONENT "dev")
set(COREVM_INSTALL_COMPONENT "dev")
set(BN256_INSTALL_COMPONENT "dev")

set( Boost_USE_MULTITHREADED      ON )
set( Boost_USE_STATIC_LIBS ON CACHE STRING "ON or OFF" )
# don't include boost mysql library as it does a find_package(OpenSSL) thus finding the system openssl which could conflict with the bundled boringssl
set( BOOST_EXCLUDE_LIBRARIES "mysql" )
add_subdirectory( boost EXCLUDE_FROM_ALL )

add_subdirectory( libfc )
add_subdirectory( builtins )
add_subdirectory( softfloat )
add_subdirectory( wasm-jit )
add_subdirectory( chaindb )
set(APPBASE_ENABLE_AUTO_VERSION OFF CACHE BOOL "enable automatic discovery of version via 'git describe'")
add_subdirectory( appbase )
add_subdirectory( custom_appbase )
add_subdirectory( chain )
add_subdirectory( testing )
add_subdirectory( version )
add_subdirectory( state_history )
add_subdirectory( cli11 )

set(USE_EXISTING_SOFTFLOAT ON CACHE BOOL "use pre-exisiting softfloat lib")
set(ENABLE_ADDRESS_SANITIZER OFF CACHE BOOL "Use address sanitizer")
set(ENABLE_UNDEFINED_BEHAVIOR_SANITIZER OFF CACHE BOOL "Use UB sanitizer")
set(ENABLE_PROFILE OFF CACHE BOOL "Enable for profile builds")
# core-vm has its own ENABLE_TESTS/ENABLE_TOOLS options that conflict with the
# top-level project options.  Force them OFF so only the core-vm library is
# built; our own test suite exercises the VM through the chain layer.
if(vm IN_LIST CORE_NET_WASM_RUNTIMES OR vm-jit IN_LIST CORE_NET_WASM_RUNTIMES)
   # CMP0077 NEW: option() honors normal variables, so our overrides stick.
   set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
   set(ENABLE_TESTS OFF)
   set(ENABLE_TOOLS OFF)
   add_subdirectory( core-vm )
   unset(ENABLE_TESTS)
   unset(ENABLE_TOOLS)
   unset(CMAKE_POLICY_DEFAULT_CMP0077)
endif()
add_subdirectory( prometheus )
