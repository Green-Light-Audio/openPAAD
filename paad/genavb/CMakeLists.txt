# GenAVB network port driver on top of the openPAAD HAL (portable core).
# Built against GenAVB's RTOS layer headers and the board's genavb_sdk.h,
# which the genavb_rtos_<target> library provides.

add_library(paad_genavb_hal STATIC hal_port.c)
target_include_directories(paad_genavb_hal PUBLIC include)
target_link_libraries(paad_genavb_hal PUBLIC paad_hal genavb_rtos_host)
target_compile_options(paad_genavb_hal PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter
  -Wno-address-of-packed-member -Wno-sign-compare)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(paad_genavb_hal PRIVATE -Wno-gnu-variable-sized-type-not-at-end)
endif()
