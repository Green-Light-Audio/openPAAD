# ATDECC conformance harness (cmake --preset conformance).
#
# A controller built on la_avdecc, the library Hive uses, checks two
# simulated devices on a network interface against the requirement matrix.
# la_avdecc is LGPL and a test-only dependency: it is fetched here, built
# into the harness and never linked into firmware.

include(FetchContent)

# la_avdecc v4.3.1.1. FetchContent would clone its submodules with their
# whole history (hundreds of MB); only four are needed, fetched shallow.
FetchContent_Declare(la_avdecc
  GIT_REPOSITORY https://github.com/L-Acoustics/avdecc.git
  GIT_TAG v4.3.1.1 # 6d61a92e7f264c69f23cdc38f50d31114e567aa0
  GIT_SHALLOW TRUE
  GIT_SUBMODULES ""
  GIT_PROGRESS TRUE
  SOURCE_SUBDIR added-below)
FetchContent_MakeAvailable(la_avdecc)
if(NOT EXISTS ${la_avdecc_SOURCE_DIR}/externals/nih/cmake/cmakeUtils/ProjectMacros.cmake)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init --depth 1
            externals/3rdparty/fmtlib externals/3rdparty/json cmake/cmakeUtils externals/nih
    WORKING_DIRECTORY ${la_avdecc_SOURCE_DIR} RESULT_VARIABLE _rc)
  if(_rc EQUAL 0)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --depth 1 cmake/cmakeUtils
      WORKING_DIRECTORY ${la_avdecc_SOURCE_DIR}/externals/nih RESULT_VARIABLE _rc)
  endif()
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Cannot fetch la_avdecc's submodules")
  endif()
endif()

# The controller library, static, with the pcap interface only.
set(BUILD_AVDECC_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_LIB_SHARED_CXX OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_LIB_STATIC_RT_SHARED ON CACHE BOOL "" FORCE)
set(BUILD_AVDECC_BINDINGS_C OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_CONTROLLER ON CACHE BOOL "" FORCE)
set(BUILD_AVDECC_INTERFACE_PCAP ON CACHE BOOL "" FORCE)
set(BUILD_AVDECC_INTERFACE_MAC OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_INTERFACE_VIRTUAL OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_INTERFACE_SERIAL OFF CACHE BOOL "" FORCE)
set(BUILD_AVDECC_INTERFACE_LOCAL OFF CACHE BOOL "" FORCE)
set(INSTALL_AVDECC_LIB_SHARED_CXX OFF CACHE BOOL "" FORCE)
set(INSTALL_AVDECC_HEADERS OFF CACHE BOOL "" FORCE)
set(INSTALL_AVDECC_BINDINGS OFF CACHE BOOL "" FORCE)
set(INSTALL_AVDECC_DOC OFF CACHE BOOL "" FORCE)
add_subdirectory(${la_avdecc_SOURCE_DIR} ${la_avdecc_BINARY_DIR} EXCLUDE_FROM_ALL)

add_executable(openpaad-conformance conformance.cpp)
target_link_libraries(openpaad-conformance PRIVATE la_avdecc_controller_static)
target_compile_features(openpaad-conformance PRIVATE cxx_std_17)
target_compile_options(openpaad-conformance PRIVATE -Wall -Wextra -Werror)

# Two simulated devices and the harness on a veth pair: Linux, with root or
# passwordless sudo (the CI has both); skipped otherwise.
add_test(NAME openpaad_conformance
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run.sh $<TARGET_FILE:openpaad-sim> $<TARGET_FILE:openpaad-conformance>
          ${CMAKE_SOURCE_DIR}/docs/requirements/matrix.csv)
set_tests_properties(openpaad_conformance PROPERTIES TIMEOUT 180 LABELS conformance SKIP_RETURN_CODE 77)
