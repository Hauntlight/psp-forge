cmake_minimum_required(VERSION 3.10)
project(demo_scenes C)

set(CMAKE_C_STANDARD 99)

if(NOT DEFINED ENV{PSPDEV})
    set(ENV{PSPDEV} "/usr/local/pspdev")
endif()
set(PSPDEV $ENV{PSPDEV})

add_executable(demo_scenes src/main.c)

target_compile_options(demo_scenes PRIVATE
    -O2 -G0 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
)

target_link_libraries(demo_scenes PRIVATE
    pspforge
    pspgum pspgu pspge
    pspaudio pspdisplay pspctrl psprtc pspfpu m
)

create_pbp_file(
    TARGET demo_scenes
    TITLE "PSP Scene Manager Demo"
    BUILD_PRX
    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png"
    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/pic1.png"
)
