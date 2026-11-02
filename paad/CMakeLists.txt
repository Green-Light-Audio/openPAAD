# openPAAD's own pro-audio layer on top of GenAVB/TSN.
#
# Built for every target (firmware and host) from the same sources.

add_library(paad STATIC
  src/bandwidth.c
)
target_include_directories(paad PUBLIC include)
target_compile_options(paad PRIVATE -Wall -Wextra -Werror $<$<COMPILE_LANGUAGE:C>:-std=c11>)
