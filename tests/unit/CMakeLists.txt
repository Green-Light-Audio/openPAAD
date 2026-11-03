# Host unit tests (GoogleTest). Run with: ctest --preset host

include(FetchContent)

find_package(GTest 1.12 QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/hal ${CMAKE_BINARY_DIR}/hal)
add_subdirectory(${CMAKE_SOURCE_DIR}/hal/sim ${CMAKE_BINARY_DIR}/hal/sim)
add_subdirectory(${CMAKE_SOURCE_DIR}/paad ${CMAKE_BINARY_DIR}/paad)
add_subdirectory(${CMAKE_SOURCE_DIR}/paad/genavb ${CMAKE_BINARY_DIR}/paad/genavb)

add_executable(openpaad_unit_tests
  srp_bandwidth_test.cpp
  genavb_shim.c
  stubs.c
  hal_headers_test.c
  hal_headers_test.cpp
  sim_clock_test.cpp
  sim_world_test.cpp
  sim_eth_test.cpp
  sim_mclk_test.cpp
)
target_link_libraries(openpaad_unit_tests PRIVATE paad genavb_protocols_host paad_hal_sim GTest::gtest_main)
target_compile_features(openpaad_unit_tests PRIVATE cxx_std_17)
target_compile_options(openpaad_unit_tests PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(genavb_shim.c PROPERTIES COMPILE_OPTIONS "-Wno-extra")
# The HAL headers must stay valid, warning-free C11 and C++17.
set_source_files_properties(hal_headers_test.c PROPERTIES COMPILE_OPTIONS "-std=c11;-pedantic")
set_source_files_properties(hal_headers_test.cpp PROPERTIES COMPILE_OPTIONS "-pedantic")

# Tests that need a running FreeRTOS (POSIX port): the GenAVB HAL port driver.
add_executable(openpaad_rtos_tests
  rtos_test_main.cpp
  genavb_hal_port_test.cpp
  genavb_hal_fixture.c
)
target_link_libraries(openpaad_rtos_tests PRIVATE paad_genavb_hal paad_hal_sim GTest::gtest)
target_compile_features(openpaad_rtos_tests PRIVATE cxx_std_17)
target_compile_options(openpaad_rtos_tests PRIVATE -Wall -Wextra -Werror)
# The C bridge includes GenAVB's OS-layer headers.
set_source_files_properties(genavb_hal_fixture.c PROPERTIES COMPILE_OPTIONS
  "-Wno-unused-parameter;-Wno-sign-compare;-Wno-address-of-packed-member;-Wno-missing-field-initializers")

# GenAVB's OS clock layer, built alone on a fake hardware clock.
add_executable(openpaad_genavb_clock_tests
  genavb_clock_test.cpp
  genavb_clock_fixture.c
  ${CMAKE_SOURCE_DIR}/third_party/genavb/rtos/clock.c
)
target_include_directories(openpaad_genavb_clock_tests PRIVATE
  $<TARGET_PROPERTY:genavb_rtos_host,INTERFACE_INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:genavb_protocols_host,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(openpaad_genavb_clock_tests PRIVATE
  $<TARGET_PROPERTY:genavb_protocols_host,INTERFACE_COMPILE_DEFINITIONS>)
target_compile_options(openpaad_genavb_clock_tests PRIVATE
  $<TARGET_PROPERTY:genavb_protocols_host,INTERFACE_COMPILE_OPTIONS>
  -Wall -Werror $<$<COMPILE_LANGUAGE:CXX>:-Wextra> $<$<COMPILE_LANGUAGE:C>:-Wno-address-of-packed-member>)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(openpaad_genavb_clock_tests PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wno-gnu-variable-sized-type-not-at-end -Wno-enum-conversion -Wno-unused-function>)
endif()
target_link_libraries(openpaad_genavb_clock_tests PRIVATE GTest::gtest_main)
target_compile_features(openpaad_genavb_clock_tests PRIVATE cxx_std_17)

include(GoogleTest)
gtest_discover_tests(openpaad_unit_tests)
gtest_discover_tests(openpaad_rtos_tests)
gtest_discover_tests(openpaad_genavb_clock_tests)
