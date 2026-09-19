cmake_minimum_required(VERSION 3.10)
project(pspforge C)

set(CMAKE_C_STANDARD 99)

if(NOT DEFINED ENV{PSPDEV})
    set(ENV{PSPDEV} "/usr/local/pspdev")
endif()

set(PSPDEV $ENV{PSPDEV})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PSPDEV}/psp/include
    ${PSPDEV}/psp/sdk/include
)

set(PSPFORGE_SOURCES
    src/core.c
    src/vram.c
    src/video2d.c
    src/video3d.c
    src/input.c
    src/audio.c
)

add_library(pspforge STATIC ${PSPFORGE_SOURCES})

target_compile_options(pspforge PRIVATE
    -O2
    -G0
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fno-strict-aliasing
)

install(TARGETS pspforge DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(FILES include/psp_forge.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
