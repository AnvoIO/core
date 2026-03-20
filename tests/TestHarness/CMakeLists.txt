# core_symbol.py is generated from a template — must stay as configure_file
configure_file(core_symbol.py.in core_symbol.py)

# All other files: symlink to source so edits are reflected immediately without re-running cmake
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
   file(GLOB _th_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.json")
   list(REMOVE_ITEM _th_files "core_symbol.py.in" "core_symbol.py")
   foreach(_f ${_th_files})
      if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${_f})
         file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/${_f} ${CMAKE_CURRENT_BINARY_DIR}/${_f} SYMBOLIC)
      endif()
   endforeach()
endif()
