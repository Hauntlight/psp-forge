cmake_minimum_required(VERSION 3.10)
project(psp_2d_game C)

set(CMAKE_C_STANDARD 99)

if(NOT DEFINED ENV{PSPDEV})
    set(ENV{PSPDEV} "/usr/local/pspdev")
endif()

set(PSPDEV $ENV{PSPDEV})

add_executable(psp_2d_game src/main.c)

target_compile_options(psp_2d_game PRIVATE
    -O2
    -G0
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fno-strict-aliasing
)

target_link_libraries(psp_2d_game PRIVATE
    pspforge
    pspgum
    pspgu
    pspge
    pspaudio
    pspdisplay
    pspctrl
    psprtc
    pspkernel
    m
)

create_pbp_file(
    TARGET psp_2d_game
    TITLE "PSP 2D Starter"
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
