# openpaad-sim: the stack on simulated hardware (host builds only).

add_executable(openpaad-sim main.c audio.c controlled.c controller.c entity.c media_clock.c)
# GenAVB's protocol code, its OS layer and the HAL driver call each other:
# GNU-style linkers need them in a group; Apple's linker resolves references
# between archives in any order.
set(_stack paad_genavb_hal genavb_aem_host genavb_rtos_host genavb_protocols_host)
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

# End to end, in virtual time: a controller connects two devices, disconnects
# and reconnects them; lossless audio and media clock recovery.
add_test(NAME openpaad_sim_audio
  COMMAND sh ${CMAKE_SOURCE_DIR}/tests/e2e/audio.sh $<TARGET_FILE:openpaad-sim> 20 100)
set_tests_properties(openpaad_sim_audio PROPERTIES TIMEOUT 400 LABELS e2e)

# End to end on a real network interface (Linux veth pair, real time): only
# with libpcap, and where root or passwordless sudo is available (CI);
# skipped otherwise.
if(OPENPAAD_SIM_HOST_NET)
  add_test(NAME openpaad_sim_net
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/e2e/net.sh $<TARGET_FILE:openpaad-sim> 20)
  set_tests_properties(openpaad_sim_net PROPERTIES TIMEOUT 120 LABELS e2e SKIP_RETURN_CODE 77)
endif()
