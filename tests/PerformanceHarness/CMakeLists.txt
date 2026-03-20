# Symlink all test files to build dir so edits are reflected immediately
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
   file(GLOB _ph_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.json")
   foreach(_f ${_ph_files})
      if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${_f})
         file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/${_f} ${CMAKE_CURRENT_BINARY_DIR}/${_f} SYMBOLIC)
      endif()
   endforeach()
endif()

add_subdirectory( CorePluginArgs )
