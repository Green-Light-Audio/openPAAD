# Toolchain file for bare-metal Arm Cortex-M targets (arm-none-eabi GCC).
#
# The compiler is looked up in ARMGCC_DIR (if set) and then on PATH, so the
# same file works in the devcontainer, in CI and on a developer machine.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(_prefix arm-none-eabi-)
if(DEFINED ENV{ARMGCC_DIR})
  set(_hint "$ENV{ARMGCC_DIR}/bin")
endif()

find_program(OPENPAAD_ARM_GCC ${_prefix}gcc HINTS ${_hint} REQUIRED)
get_filename_component(_bin_dir "${OPENPAAD_ARM_GCC}" DIRECTORY)

set(CMAKE_C_COMPILER   "${_bin_dir}/${_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_bin_dir}/${_prefix}g++")
set(CMAKE_ASM_COMPILER "${_bin_dir}/${_prefix}gcc")
set(CMAKE_AR           "${_bin_dir}/${_prefix}ar" CACHE FILEPATH "" FORCE)
set(CMAKE_OBJCOPY      "${_bin_dir}/${_prefix}objcopy" CACHE FILEPATH "" FORCE)
set(CMAKE_SIZE         "${_bin_dir}/${_prefix}size" CACHE FILEPATH "" FORCE)

# Compiler checks must not try to link a hosted executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
