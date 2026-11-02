# openpaad-sim: the stack on simulated hardware (host builds only).

add_executable(openpaad-sim main.c)
# GenAVB's protocol code, its OS layer and the HAL driver call each other:
# GNU-style linkers need them in a group; Apple's linker resolves references
# between archives in any order.
set(_stack paad_genavb_hal genavb_rtos_host genavb_protocols_host)
if(APPLE)
  target_link_libraries(openpaad-sim PRIVATE paad_sim_platform ${_stack})
else()
  target_link_libraries(openpaad-sim PRIVATE paad_sim_platform -Wl,--start-group ${_stack} -Wl,--end-group)
endif()
target_compile_options(openpaad-sim PRIVATE -Wall -Wextra -Werror)

# End to end: GenAVB's gPTP must bring port 1's clock onto port 0's.
add_test(NAME openpaad_sim_gptp_sync COMMAND openpaad-sim -t 20 --check-gptp 1000)
set_tests_properties(openpaad_sim_gptp_sync PROPERTIES TIMEOUT 90 LABELS e2e)
