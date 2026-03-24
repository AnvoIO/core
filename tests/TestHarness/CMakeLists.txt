# core_symbol.py is generated from a template
configure_file(core_symbol.py.in core_symbol.py)

# Copy all Python/JSON files from source into the build directory.
# Symlinks break Python's package resolution: when __init__.py is a symlink,
# Python resolves the real path and looks for sibling modules in the source
# directory — missing the generated core_symbol.py that only exists in the
# build directory.
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
   file(GLOB _th_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.json")
   list(REMOVE_ITEM _th_files "core_symbol.py.in" "core_symbol.py")
   foreach(_f ${_th_files})
      configure_file(${_f} ${_f} COPYONLY)
   endforeach()
endif()
