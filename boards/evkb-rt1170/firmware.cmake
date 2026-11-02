# Firmware image support for the NXP MIMXRT1170-EVKB (Cortex-M7, XIP from QSPI flash).
#
# Defines:
#   openpaad_board              object library: SDK drivers, board bring-up, FreeRTOS
#                               (objects, not an archive, so that the boot header
#                               and system calls are always linked in)
#   openpaad_add_firmware(...)  helper that links an application into an .elf/.bin/.hex
#
# All SDK sources come from the west workspace (see west.yml). The NXP board
# files (pin mux, clocks, MPU, debug console) are used unmodified from the
# public MCUXpresso SDK examples repository.

set(_sdk "${OPENPAAD_DEPS_DIR}/mcuxsdk")
set(_dev "${_sdk}/devices/RT/RT1170/MIMXRT1176")
set(_evk "${_sdk}/examples/_boards/evkbmimxrt1170")
set(_rtos "${_sdk}/rtos/freertos/freertos-kernel")
set(_pinmux_dir "${_evk}/driver_examples/enet_1g/txrx_multiring_transfer")

if(NOT EXISTS "${_evk}/board.c")
  message(FATAL_ERROR
    "EVKB board files missing. Enable them with:\n"
    "  west config manifest.group-filter -- +board-files && west update")
endif()

set(OPENPAAD_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/linker/openpaad_cm7_flexspi_nor.ld")

add_library(openpaad_board OBJECT
  # Device start-up and boot header
  ${_dev}/gcc/startup_MIMXRT1176_cm7.S
  ${_dev}/system_MIMXRT1176_cm7.c
  ${_dev}/xip/fsl_flexspi_nor_boot.c
  ${_evk}/xip/evkbmimxrt1170_flexspi_nor_config.c
  # SoC drivers
  ${_dev}/drivers/fsl_clock.c
  ${_dev}/drivers/power/fsl_pmu.c
  ${_dev}/drivers/power/fsl_dcdc.c
  ${_dev}/drivers/power/fsl_gpc.c
  ${_dev}/drivers/power/fsl_soc_src.c
  ${_dev}/drivers/anatop_ai/fsl_anatop_ai.c
  ${_sdk}/drivers/common/fsl_common.c
  ${_sdk}/drivers/common/fsl_common_arm.c
  ${_sdk}/drivers/cache/armv7-m7/fsl_cache.c
  ${_sdk}/drivers/igpio/fsl_gpio.c
  ${_sdk}/drivers/lpuart/fsl_lpuart.c
  ${_sdk}/drivers/enet/fsl_enet.c
  ${_sdk}/drivers/gpt/fsl_gpt.c
  # Components
  ${_sdk}/components/debug_console_lite/fsl_debug_console.c
  ${_sdk}/components/str/fsl_str.c
  ${_sdk}/components/uart/fsl_adapter_lpuart.c
  ${_sdk}/components/phy/device/phyrtl8211f/fsl_phyrtl8211f.c
  # NXP EVKB board files
  ${_evk}/board.c
  ${_evk}/common/clock/clock_config.c
  ${_pinmux_dir}/pin_mux.c
  # openPAAD board services
  ${CMAKE_CURRENT_LIST_DIR}/src/board_init.c
  ${CMAKE_CURRENT_LIST_DIR}/src/board_genavb.c
  ${CMAKE_CURRENT_LIST_DIR}/src/freertos_heap.c
  ${CMAKE_CURRENT_LIST_DIR}/src/syscalls.c
  # FreeRTOS kernel
  ${_rtos}/tasks.c
  ${_rtos}/queue.c
  ${_rtos}/list.c
  ${_rtos}/timers.c
  ${_rtos}/event_groups.c
  ${_rtos}/stream_buffer.c
  ${_rtos}/portable/GCC/ARM_CM4F/port.c
  ${_rtos}/portable/MemMang/heap_4.c
)

target_include_directories(openpaad_board PUBLIC
  ${OPENPAAD_BOARD_INCLUDE}
  ${CMAKE_SOURCE_DIR}/third_party/genavb/include
  ${CMAKE_SOURCE_DIR}/third_party/genavb/include/rtos
  ${CMAKE_BINARY_DIR}/generated
)

# Vendor headers are included as system headers so that openPAAD's own code
# can build with -Wextra -Werror without tripping over them.
target_include_directories(openpaad_board SYSTEM PUBLIC
  ${_dev}
  ${_dev}/drivers
  ${_dev}/drivers/power
  ${_dev}/drivers/anatop_ai
  ${_dev}/xip
  ${_sdk}/devices/RT/RT1170/periph
  ${_sdk}/arch/arm/CMSIS/Core/Include
  ${_sdk}/drivers/common
  ${_sdk}/drivers/cache/armv7-m7
  ${_sdk}/drivers/igpio
  ${_sdk}/drivers/lpuart
  ${_sdk}/drivers/enet
  ${_sdk}/drivers/gpt
  ${_sdk}/components/debug_console_lite
  ${_sdk}/components/uart
  ${_sdk}/components/phy
  ${_sdk}/components/phy/device/phyrtl8211f
  ${_evk}
  ${_evk}/common/clock
  ${_evk}/xip
  ${_pinmux_dir}
  ${_rtos}/include
  ${_rtos}/portable/GCC/ARM_CM4F
  ${_sdk}/components/str
)

target_compile_definitions(openpaad_board PUBLIC
  ${OPENPAAD_CPU_DEFINES}
  MCUXPRESSO_SDK
  XIP_EXTERNAL_FLASH=1
  XIP_BOOT_HEADER_ENABLE=1
  SERIAL_PORT_TYPE_UART=1
  SDK_DEBUGCONSOLE=1
  SDK_OS_FREE_RTOS
  ENET_ENHANCEDBUFFERDESCRIPTOR_MODE
  __STARTUP_CLEAR_BSS
  __STARTUP_INITIALIZE_NONCACHEDATA
  __STARTUP_INITIALIZE_RAMFUNCTION
)

target_compile_options(openpaad_board PUBLIC
  ${OPENPAAD_CPU_FLAGS}
  $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
  -ffunction-sections -fdata-sections -fno-common
  $<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>
)

# Our own sources build with warnings as errors; NXP sources keep their defaults.
set_source_files_properties(
  ${CMAKE_CURRENT_LIST_DIR}/src/board_init.c
  ${CMAKE_CURRENT_LIST_DIR}/src/board_genavb.c
  ${CMAKE_CURRENT_LIST_DIR}/src/freertos_heap.c
  ${CMAKE_CURRENT_LIST_DIR}/src/syscalls.c
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
)

# openpaad_add_firmware(<name> SOURCES <src>...)
function(openpaad_add_firmware name)
  cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

  add_executable(${name} ${ARG_SOURCES})
  set_target_properties(${name} PROPERTIES SUFFIX ".elf")
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)

  target_link_libraries(${name} PRIVATE
    -Wl,--start-group
    openpaad_board
    paad
    stack-rtos
    ${CMAKE_BINARY_DIR}/third_party/genavb/libstack-core.a
    -Wl,--end-group
  )
  add_dependencies(${name} stack-core)

  target_link_options(${name} PRIVATE
    ${OPENPAAD_CPU_FLAGS}
    -T${OPENPAAD_LINKER_SCRIPT}
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map
    -Wl,--defsym=__stack_size__=0x2000
    -Wl,--defsym=__heap_size__=0x4000
    -Wl,--no-warn-rwx-segments
  )

  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
    COMMENT "Creating ${name}.bin and ${name}.hex"
  )
endfunction()
