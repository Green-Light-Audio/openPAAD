# NXP MIMXRT1170-EVKB, Cortex-M7 core.
#
# Variables consumed by the top-level CMakeLists.txt.

set(GENAVB_TARGET freertos_rt1176)
set(OPENPAAD_BOARD_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/include)
set(OPENPAAD_CPU_FLAGS -mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16)
set(OPENPAAD_CPU_DEFINES CPU_MIMXRT1176DVMAA_cm7)
