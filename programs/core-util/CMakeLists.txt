add_executable( ${CORE_NET_UTIL_EXECUTABLE_NAME} main.cpp actions/subcommand.cpp actions/generic.cpp actions/blocklog.cpp actions/bls.cpp actions/snapshot.cpp actions/chain.cpp)

if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()

target_include_directories(${CORE_NET_UTIL_EXECUTABLE_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries( ${CORE_NET_UTIL_EXECUTABLE_NAME}
        PRIVATE appbase version
        PRIVATE core_net_chain chain_plugin fc spring-cli11 producer_plugin ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

copy_bin( ${CORE_NET_UTIL_EXECUTABLE_NAME} )
install( TARGETS
  ${CORE_NET_UTIL_EXECUTABLE_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} COMPONENT base
)
