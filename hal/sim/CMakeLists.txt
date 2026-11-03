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
  src/mclk.c
)
target_include_directories(paad_hal_sim PUBLIC include)
target_link_libraries(paad_hal_sim PUBLIC paad_hal Threads::Threads)

# Host network interfaces (paad_sim_eth_connect_host) through libpcap: part of
# macOS, libpcap-dev on Linux. Without it (or with OPENPAAD_SIM_PCAP=OFF),
# simulated ports only take cables.
option(OPENPAAD_SIM_PCAP "Let simulated ports use host network interfaces (libpcap)" ON)
if(OPENPAAD_SIM_PCAP)
  find_path(PCAP_INCLUDE_DIR pcap/pcap.h HINTS ${CMAKE_OSX_SYSROOT}/usr/include)
  find_library(PCAP_LIBRARY pcap HINTS ${CMAKE_OSX_SYSROOT}/usr/lib)
endif()
if(OPENPAAD_SIM_PCAP AND PCAP_INCLUDE_DIR AND PCAP_LIBRARY)
  target_sources(paad_hal_sim PRIVATE src/host_if.c)
  target_include_directories(paad_hal_sim PRIVATE ${PCAP_INCLUDE_DIR})
  target_link_libraries(paad_hal_sim PUBLIC ${PCAP_LIBRARY})
  set(OPENPAAD_SIM_HOST_NET ON CACHE INTERNAL "simulated ports can use host interfaces")
else()
  message(STATUS "Without libpcap: simulated ports cannot use host network interfaces")
  target_sources(paad_hal_sim PRIVATE src/host_if_none.c)
  set(OPENPAAD_SIM_HOST_NET OFF CACHE INTERNAL "simulated ports can use host interfaces")
endif()
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
  src/freertos_hooks.c
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
# Virtual time: the port's tick thread sleeps through a gate (src/freertos_hooks.c).
set_source_files_properties(${_freertos}/portable/ThirdParty/GCC/Posix/port.c
  PROPERTIES COMPILE_DEFINITIONS "usleep=paad_sim_port_usleep")

# --- GenAVB OS layer and stack tasks on the host -----------------------------------
# GenAVB's RTOS layer, API and component tasks, built unchanged. NXP hardware
# drivers are left out: network ports, clocks and timers come from the
# openPAAD HAL through paad/genavb.

file(GLOB _rtos_srcs CONFIGURE_DEPENDS ${_genavb}/rtos/*.c)
# NXP drivers compile to stubs when the board declares none of their devices;
# the audio PLL driver is replaced by the HAL media clock.
list(FILTER _rtos_srcs EXCLUDE REGEX "/rtos/imx-pll\\.c$")
file(GLOB _api_os_srcs CONFIGURE_DEPENDS ${_genavb}/api/rtos/*.c)
list(FILTER _api_os_srcs EXCLUDE REGEX "/api/rtos/(dsa)\\.c$")

set(_genavb_generated ${CMAKE_CURRENT_BINARY_DIR}/genavb_generated)
file(WRITE ${_genavb_generated}/common/version.h
  "/* Generated by openPAAD. */\n#ifndef _VERSION_H_\n#define _VERSION_H_\n#define GENAVB_VERSION \"7.6.2\"\n#endif\n")

add_library(genavb_rtos_host STATIC
  ${_rtos_srcs}
  ${_api_os_srcs}
  ${_genavb}/api/error.c
  ${_genavb}/api/control.c
  ${_genavb}/api/clock.c
  ${_genavb}/api/version.c
  ${_genavb}/api/managed_objects.c
  ${_genavb}/api/managed_objects_gptp.c
  ${_genavb}/api/socket.c
  ${_genavb}/api/streaming.c
  ${_genavb}/gptp/rtos/main.c
  ${_genavb}/srp/rtos/main.c
  ${_genavb}/maap/rtos/main.c
  ${_genavb}/avtp/rtos/main.c
  ${_genavb}/avdecc/rtos/main.c
  ${_genavb}/management/rtos/main.c
)
target_include_directories(genavb_rtos_host PUBLIC ${_genavb}/rtos ${_genavb_generated})
target_link_libraries(genavb_rtos_host PUBLIC genavb_protocols_host freertos_posix)
target_compile_options(genavb_rtos_host PRIVATE -Wall -Werror -Wno-address-of-packed-member)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  # Also: abs() of an unsigned difference in the media clock recovery code
  # (correct through the implicit conversion to int), and static inline
  # helpers left unused in some configurations.
  target_compile_options(genavb_rtos_host PRIVATE -Wno-gnu-variable-sized-type-not-at-end -Wno-enum-conversion
    -Wno-absolute-value -Wno-unused-function)
endif()

# --- GenAVB's ATDECC entity models ----------------------------------------------------
# Reference entity definitions shipped with GenAVB (descriptor tables) and its
# helpers that check and load them, built unchanged.

set(_aem "${_genavb}/apps/common/aem-manager")
add_library(genavb_aem_host STATIC
  ${_genavb}/apps/rtos/aem-manager/aem_manager_rtos.c
  ${_aem}/aem_manager_helpers.c
  ${_aem}/listener_talker_audio_single_milan.c
  ${_aem}/avdecc_controller.c
)
target_include_directories(genavb_aem_host PUBLIC ${_aem} ${_genavb}/apps/rtos/aem-manager)
target_link_libraries(genavb_aem_host PUBLIC genavb_rtos_host)
target_compile_options(genavb_aem_host PRIVATE -Wall -Werror)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  # A 16-bit count checked against a limit it cannot exceed.
  target_compile_options(genavb_aem_host PRIVATE -Wno-tautological-constant-out-of-range-compare)
endif()

# --- The simulated board and its FreeRTOS glue (openpaad-sim) ----------------------

add_library(paad_sim_platform STATIC src/platform.c src/freertos.c)
target_link_libraries(paad_sim_platform PUBLIC paad_hal_sim freertos_posix)
target_compile_options(paad_sim_platform PRIVATE -Wall -Wextra -Werror $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)
