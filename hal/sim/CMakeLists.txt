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

# --- Simulator HAL port --------------------------------------------------------
# Implements the openPAAD HAL (hal/include) on the host.

find_package(Threads REQUIRED)

add_library(paad_hal_sim STATIC
  src/clock.c
  src/world.c
  src/eth.c
)
target_include_directories(paad_hal_sim PUBLIC include)
target_link_libraries(paad_hal_sim PUBLIC paad_hal Threads::Threads)
target_compile_options(paad_hal_sim PRIVATE -Wall -Wextra -Werror $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)

# --- FreeRTOS on the host --------------------------------------------------------
# The FreeRTOS kernel with its POSIX port (tasks are host threads) and the NXP
# RTOS abstraction layer GenAVB is written against.

file(GLOB _ral_srcs CONFIGURE_DEPENDS ${_ral}/*.c)

add_library(freertos_posix STATIC
  ${_freertos}/tasks.c
  ${_freertos}/queue.c
  ${_freertos}/list.c
  ${_freertos}/timers.c
  ${_freertos}/event_groups.c
  ${_freertos}/stream_buffer.c
  ${_freertos}/portable/MemMang/heap_3.c
  ${_freertos}/portable/ThirdParty/GCC/Posix/port.c
  ${_freertos}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
  ${_ral_srcs}
  src/console.c
)
target_include_directories(freertos_posix PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${_freertos}/include
  ${_freertos}/portable/ThirdParty/GCC/Posix
  ${_freertos}/portable/ThirdParty/GCC/Posix/utils
  ${_ral}/include
)
target_link_libraries(freertos_posix PUBLIC Threads::Threads)
target_compile_options(freertos_posix PRIVATE -w) # third-party code, built as is

# --- GenAVB OS layer on the host --------------------------------------------------
# The parts of GenAVB's RTOS layer that run on the simulator, built unchanged.
# Grows with openpaad-sim; hardware drivers are replaced by paad/genavb.

add_library(genavb_rtos_host STATIC
  ${_genavb}/rtos/hw_clock.c
  ${_genavb}/rtos/hw_timer.c
  ${_genavb}/rtos/log.c
  ${_genavb}/rtos/prng_32.c
  ${_genavb}/rtos/stdlib.c
)
target_include_directories(genavb_rtos_host PUBLIC ${_genavb}/rtos)
target_link_libraries(genavb_rtos_host PUBLIC genavb_protocols_host freertos_posix)
target_compile_options(genavb_rtos_host PRIVATE -Wall -Werror -Wno-address-of-packed-member)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(genavb_rtos_host PRIVATE -Wno-gnu-variable-sized-type-not-at-end -Wno-enum-conversion)
endif()
