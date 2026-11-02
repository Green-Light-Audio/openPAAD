# openPAAD hardware abstraction layer: interface headers only.
# Ports (hal/sim, hal/rt1170) implement it.

if(NOT TARGET paad_hal)
  add_library(paad_hal INTERFACE)
  target_include_directories(paad_hal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()
