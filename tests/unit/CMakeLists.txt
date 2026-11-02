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

add_subdirectory(${CMAKE_SOURCE_DIR}/hal/sim ${CMAKE_BINARY_DIR}/hal/sim)
add_subdirectory(${CMAKE_SOURCE_DIR}/paad ${CMAKE_BINARY_DIR}/paad)

add_executable(openpaad_unit_tests
  srp_bandwidth_test.cpp
  genavb_shim.c
  stubs.c
)
target_link_libraries(openpaad_unit_tests PRIVATE paad genavb_protocols_host GTest::gtest_main)
target_compile_features(openpaad_unit_tests PRIVATE cxx_std_17)
target_compile_options(openpaad_unit_tests PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(genavb_shim.c PROPERTIES COMPILE_OPTIONS "-Wno-extra")

include(GoogleTest)
gtest_discover_tests(openpaad_unit_tests)
