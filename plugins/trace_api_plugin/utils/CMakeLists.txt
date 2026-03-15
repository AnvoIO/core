add_executable( core-trace-api-util
        compress_cmd.cpp
        trace_api_util.cpp
)
target_link_libraries( core-trace-api-util trace_api_plugin )
target_include_directories( core-trace-api-util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

copy_bin( core-trace-api-util )
install( TARGETS
    core-trace-api-util

    COMPONENT base

    RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
)
