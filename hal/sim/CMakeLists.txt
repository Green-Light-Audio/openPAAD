# Host build of the GenAVB/TSN protocol code (gPTP, SRP, MAAP, AVTP, AVDECC,
# management, public helpers) for unit tests and, from Phase 1, the network simulator.
#
# The protocol code is compiled unchanged from third_party/genavb against the
# FreeRTOS POSIX port headers. See docs/decisions/0005-simulator-approach.md.

set(_genavb "${CMAKE_SOURCE_DIR}/third_party/genavb")
set(_freertos "${OPENPAAD_DEPS_DIR}/mcuxsdk/rtos/freertos/freertos-kernel")
set(_ral "${OPENPAAD_DEPS_DIR}/rtos-abstraction-layer/freertos")

if(NOT EXISTS "${_freertos}/include/FreeRTOS.h")
  message(FATAL_ERROR "FreeRTOS kernel not found in ${_freertos}; run scripts/bootstrap.sh")
endif()

file(GLOB _protocol_srcs CONFIGURE_DEPENDS
  ${_genavb}/common/*.c
  ${_genavb}/gptp/*.c
  ${_genavb}/srp/*.c
  ${_genavb}/maap/*.c
  ${_genavb}/avtp/*.c
  ${_genavb}/avdecc/*.c
  ${_genavb}/management/*.c
  ${_genavb}/public/*.c
)

add_library(genavb_protocols_host STATIC ${_protocol_srcs})

target_include_directories(genavb_protocols_host PUBLIC
  ${_genavb}/include
  ${_genavb}/include/rtos
  ${_genavb}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${_freertos}/include
  ${_freertos}/portable/ThirdParty/GCC/Posix
  ${_ral}/include
)

target_compile_options(genavb_protocols_host PUBLIC
  "SHELL:-iquote ${_genavb}"
  "SHELL:-iquote ${_genavb}/rtos/include"
  "SHELL:-iquote ${_genavb}/api/rtos"
  $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
)
target_compile_options(genavb_protocols_host PRIVATE -Wall -Werror -Wno-address-of-packed-member)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  # Clang (and Apple Clang on macOS) flags two harmless GenAVB habits that GCC
  # accepts: a GNU extension for variable-sized structs not at the end of a
  # struct, and implicit conversion between its two log-level enums.
  target_compile_options(genavb_protocols_host PRIVATE
    -Wno-gnu-variable-sized-type-not-at-end
    -Wno-enum-conversion)
endif()

target_compile_definitions(genavb_protocols_host PUBLIC
  CONFIG_GENAVB_TSN_API=1
  CONFIG_GENAVB_TSN_AVDECC=1
  CONFIG_GENAVB_TSN_AVTP=1
  CONFIG_GENAVB_TSN_GPTP=1
  CONFIG_GENAVB_TSN_MAAP=1
  CONFIG_GENAVB_TSN_MANAGEMENT=1
  CONFIG_GENAVB_TSN_SOCKET=1
  CONFIG_GENAVB_TSN_SRP=1
)
