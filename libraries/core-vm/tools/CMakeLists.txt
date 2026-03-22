# ##################################################################################################
# Create the core-vm interpreter driver.
# ##################################################################################################
add_executable(core-vm-interp ${CMAKE_CURRENT_SOURCE_DIR}/interp.cpp)
target_link_libraries(core-vm-interp core-vm)

add_executable(bench-interp ${CMAKE_CURRENT_SOURCE_DIR}/bench_interp.cpp)
target_link_libraries(bench-interp core-vm)

add_executable(hello-driver ${CMAKE_CURRENT_SOURCE_DIR}/hello_driver.cpp)
target_link_libraries(hello-driver core-vm)

add_executable(core-vm-addr2line addr2line.cpp)
target_link_libraries(core-vm-addr2line core-vm)
