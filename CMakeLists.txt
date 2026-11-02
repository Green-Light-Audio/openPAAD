# openPAAD top-level build.
#
# Two kinds of build share this file:
#   - firmware: cross-compiled for a board (cmake --preset evkb-rt1170)
#   - host:     unit tests and simulation on the build machine (cmake --preset host)

cmake_minimum_required(VERSION 3.22)

project(openpaad VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(OPENPAAD_BOARD "evkb-rt1170" CACHE STRING "Board to build firmware for, or 'host'")
set(OPENPAAD_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps" CACHE PATH
    "Directory holding the west-managed dependencies (see west.yml)")
set(OPENPAAD_GENAVB_CONFIG "endpoint_avb" CACHE STRING "GenAVB stack configuration")

get_filename_component(OPENPAAD_DEPS_DIR "${OPENPAAD_DEPS_DIR}" ABSOLUTE)

if(OPENPAAD_BOARD STREQUAL "host")
  enable_testing()
  add_subdirectory(tests/unit)
  return()
endif()

if(NOT EXISTS "${OPENPAAD_DEPS_DIR}/mcuxsdk/drivers")
  message(FATAL_ERROR
    "Dependencies not found in ${OPENPAAD_DEPS_DIR}.\n"
    "Run scripts/bootstrap.sh (west init -l openpaad && west update) first.")
endif()

include(boards/${OPENPAAD_BOARD}/board.cmake)

# Reproducible firmware: no absolute build paths in the image, so the same
# commit gives the same binary on any machine.
add_compile_options(
  -ffile-prefix-map=${OPENPAAD_DEPS_DIR}=deps
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=openpaad
)

# --- GenAVB/TSN stack --------------------------------------------------------
# GenAVB's own CMake reads these variables; see third_party/genavb/README.md.
set(TARGET ${GENAVB_TARGET})
set(CONFIG ${OPENPAAD_GENAVB_CONFIG})
set(MCUX_SDK "${OPENPAAD_DEPS_DIR}/mcuxsdk")
set(RTOS_APPS "${OPENPAAD_DEPS_DIR}/rtos-apps")
set(RTOS_ABSTRACTION_LAYER_DIR "${OPENPAAD_DEPS_DIR}/rtos-abstraction-layer/freertos")
set(FREERTOS_CONFIG_INCLUDES "${OPENPAAD_BOARD_INCLUDE}")
set(APP_GENAVB_SDK_INCLUDE "${OPENPAAD_BOARD_INCLUDE}")

add_subdirectory(third_party/genavb)

# --- Version header ------------------------------------------------------------
find_package(Git QUIET)
set(OPENPAAD_GIT_DESCRIBE "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE OPENPAAD_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/openpaad/version.h @ONLY)

# --- openPAAD pro-audio layer and HAL ------------------------------------------
add_subdirectory(paad)
target_compile_options(paad PRIVATE ${OPENPAAD_CPU_FLAGS})

add_subdirectory(hal)
# The HAL headers must also be valid C11 for the target compiler.
add_library(paad_hal_check OBJECT tests/unit/hal_headers_test.c)
target_link_libraries(paad_hal_check PRIVATE paad_hal)
target_compile_options(paad_hal_check PRIVATE ${OPENPAAD_CPU_FLAGS} -std=c11 -pedantic -Wall -Wextra -Werror)

# --- Firmware images ---------------------------------------------------------
include(boards/${OPENPAAD_BOARD}/firmware.cmake)
add_subdirectory(apps/ref-endpoint)
