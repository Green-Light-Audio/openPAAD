# openpaad-sim: the stack on simulated hardware (host builds only).

add_executable(openpaad-sim main.c audio.c)
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
add_test(NAME openpaad_sim_gptp_sync COMMAND openpaad-sim --virtual -t 15 --check-gptp 100)
set_tests_properties(openpaad_sim_gptp_sync PROPERTIES TIMEOUT 120 LABELS e2e)

# End to end: two devices, one process each, joined by a virtual cable.
add_test(NAME openpaad_sim_two_devices
  COMMAND sh ${CMAKE_SOURCE_DIR}/tests/e2e/two_devices.sh $<TARGET_FILE:openpaad-sim> 25 100)
set_tests_properties(openpaad_sim_two_devices PROPERTIES TIMEOUT 120 LABELS e2e)

# End to end: lossless audio between two devices, in virtual time.
add_test(NAME openpaad_sim_audio
  COMMAND sh ${CMAKE_SOURCE_DIR}/tests/e2e/audio.sh $<TARGET_FILE:openpaad-sim> 10 100)
set_tests_properties(openpaad_sim_audio PROPERTIES TIMEOUT 240 LABELS e2e)
