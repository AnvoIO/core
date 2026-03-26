configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/core_net/chain/core_symbol.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/core_net/chain/core_symbol.hpp)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/genesis_state_root_key.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/genesis_state_root_key.cpp)

file(GLOB HEADERS "include/core_net/chain/*.hpp"
                  "include/core_net/chain/webassembly/*.hpp"
                  "${CMAKE_CURRENT_BINARY_DIR}/include/core_net/chain/core_symbol.hpp" )

if((APPLE AND UNIX) OR (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD"))
   set(PLATFORM_TIMER_IMPL platform_timer_kqueue.cpp)
else()
   try_run(POSIX_TIMER_TEST_RUN_RESULT POSIX_TIMER_TEST_COMPILE_RESULT ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform_timer_posix_test.c)
   if(POSIX_TIMER_TEST_RUN_RESULT EQUAL 0)
      set(PLATFORM_TIMER_IMPL platform_timer_posix.cpp)
      set(CHAIN_RT_LINKAGE rt)
   else()
      set(PLATFORM_TIMER_IMPL platform_timer_asio_fallback.cpp)
   endif()
endif()

if("vm-oc" IN_LIST CORE_NET_WASM_RUNTIMES)
   enable_language(ASM)
   set(CHAIN_COREVMOC_SOURCES webassembly/runtimes/core-vm-oc/code_cache.cpp
                             webassembly/runtimes/core-vm-oc/executor.cpp
                             webassembly/runtimes/core-vm-oc/memory.cpp
                             webassembly/runtimes/core-vm-oc/intrinsic.cpp
                             webassembly/runtimes/core-vm-oc/LLVMJIT.cpp
                             webassembly/runtimes/core-vm-oc/LLVMEmitIR.cpp
                             webassembly/runtimes/core-vm-oc/compile_monitor.cpp
                             webassembly/runtimes/core-vm-oc/compile_trampoline.cpp
                             webassembly/runtimes/core-vm-oc/ipc_helpers.cpp
                             webassembly/runtimes/core-vm-oc/gs_seg_helpers.c
                             webassembly/runtimes/core-vm-oc/stack.cpp
                             $<IF:$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>,webassembly/runtimes/core-vm-oc/switch_stack_linux_aarch64.s,webassembly/runtimes/core-vm-oc/switch_stack_linux.s>
                             webassembly/runtimes/core-vm-oc.cpp
                             webassembly/runtimes/core-vm-oc/default_real_main.cpp)

   set_source_files_properties(webassembly/runtimes/core-vm-oc/LLVMJIT.cpp PROPERTIES COMPILE_FLAGS "--std=gnu++17")
   set_source_files_properties(webassembly/runtimes/core-vm-oc/LLVMEmitIR.cpp PROPERTIES COMPILE_FLAGS "--std=gnu++17")
   set_source_files_properties(webassembly/runtimes/core-vm-oc/switch_stack_linux.s PROPERTIES COMPILE_FLAGS "-Wno-unused-command-line-argument")

   llvm_map_components_to_libnames(LLVM_LIBS support core passes mcjit native orcjit)
   include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
   add_definitions(${LLVM_DEFINITIONS})
endif()

if("vm" IN_LIST CORE_NET_WASM_RUNTIMES OR "vm-jit" IN_LIST CORE_NET_WASM_RUNTIMES)
   set(CHAIN_COREVM_SOURCES "webassembly/runtimes/core-vm.cpp")
   set(CHAIN_COREVM_LIBRARIES core-vm)
   # Files that include signals.hpp (via backend.hpp) use siglongjmp from a SIGSEGV
   # handler to jump across stacks (JIT execution stack -> thread stack). glibc's
   # __longjmp_chk (_FORTIFY_SOURCE) treats cross-stack longjmp as stack corruption
   # and aborts on AArch64. Disable fortification for all affected translation units.
   set_source_files_properties(webassembly/runtimes/core-vm.cpp PROPERTIES COMPILE_FLAGS "-U_FORTIFY_SOURCE")
   set_source_files_properties(webassembly/softfloat.cpp PROPERTIES COMPILE_FLAGS "-U_FORTIFY_SOURCE")
   set_source_files_properties(wasm_interface.cpp PROPERTIES COMPILE_FLAGS "-U_FORTIFY_SOURCE")
endif()

set(CHAIN_WEBASSEMBLY_SOURCES
   webassembly/action.cpp
   webassembly/authorization.cpp
   webassembly/cf_system.cpp
   webassembly/cf_transaction.cpp
   webassembly/compiler_builtins.cpp
   webassembly/context_free.cpp
   webassembly/console.cpp
   webassembly/crypto.cpp
   webassembly/database.cpp
   webassembly/memory.cpp
   webassembly/permission.cpp
   webassembly/privileged.cpp
   webassembly/producer.cpp
   webassembly/softfloat.cpp
   webassembly/system.cpp
   webassembly/transaction.cpp
)

add_library(eosio_rapidjson INTERFACE)
target_include_directories(eosio_rapidjson INTERFACE ../rapidjson/include)

## SORT .cpp by most likely to change / break compile
add_library( core_net_chain
             name.cpp
             transaction.cpp
             block.cpp
             block_handle.cpp
             block_header.cpp
             block_header_state.cpp
             block_state.cpp
             block_header_state_legacy.cpp
             block_state_legacy.cpp
             finalizer.cpp
             fork_database.cpp
             qc.cpp
             finality_core.cpp
             controller.cpp
             authorization_manager.cpp
             resource_limits.cpp
             block_log.cpp
             transaction_context.cpp
             system_contract.cpp
             system_contract_abi.cpp
             system_contract_abi_bin.cpp
             chain_config.cpp
             chain_id_type.cpp
             config.cpp
             genesis_state.cpp
             ${CMAKE_CURRENT_BINARY_DIR}/genesis_state_root_key.cpp

              wast_to_wasm.cpp
              wasm_interface.cpp
              wasm_validation.cpp
              wasm_injection.cpp
              wasm_config.cpp
              apply_context.cpp
              abi_serializer.cpp
              asset.cpp
              snapshot.cpp
              snapshot_scheduler.cpp
              deep_mind.cpp

             ${CHAIN_COREVMOC_SOURCES}
             ${CHAIN_COREVM_SOURCES}
             ${CHAIN_WEBASSEMBLY_SOURCES}

             authority.cpp
             trace.cpp
             transaction_metadata.cpp
             protocol_state_object.cpp
             protocol_feature_activation.cpp
             protocol_feature_manager.cpp
             producer_schedule.cpp
             genesis_intrinsics.cpp
             symbol.cpp
             whitelisted_intrinsics.cpp
             thread_utils.cpp
             peer_keys_db.cpp
             platform_timer_accuracy.cpp
             ${PLATFORM_TIMER_IMPL}
             ${HEADERS}
             )



target_link_libraries( core_net_chain PUBLIC bn256 fc chainbase eosio_rapidjson Logging IR WAST WASM
                       softfloat builtins ${CHAIN_COREVM_LIBRARIES} ${LLVM_LIBS} ${CHAIN_RT_LINKAGE}
                       Boost::signals2 Boost::hana Boost::property_tree Boost::multi_index Boost::asio Boost::lockfree
                       Boost::assign Boost::accumulators Boost::rational
                     )
target_include_directories( core_net_chain
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
                            )

add_library(core_net_chain_wrap INTERFACE )
target_link_libraries(core_net_chain_wrap INTERFACE core_net_chain)

if("vm-oc" IN_LIST CORE_NET_WASM_RUNTIMES)
   target_link_libraries(core_net_chain_wrap INTERFACE "-Wl,-wrap=main")
endif()

foreach(RUNTIME ${CORE_NET_WASM_RUNTIMES})
   string(TOUPPER "${RUNTIME}" RUNTIMEUC)
   string(REPLACE "-" "_" RUNTIMEUC ${RUNTIMEUC})
   target_compile_definitions(core_net_chain PUBLIC "CORE_NET_${RUNTIMEUC}_RUNTIME_ENABLED")
endforeach()

install( TARGETS core_net_chain
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} COMPONENT dev EXCLUDE_FROM_ALL
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)
install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/core_net/chain/
      DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/core_net/chain
      COMPONENT dev EXCLUDE_FROM_ALL
      FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h" EXCLUDE
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/core_net/chain/core_symbol.hpp DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/core_net/chain COMPONENT dev EXCLUDE_FROM_ALL)
