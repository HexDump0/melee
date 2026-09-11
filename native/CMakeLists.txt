cmake_minimum_required(VERSION 3.16)
project(melee_native C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

add_executable(melee
    main.c
    platform/disc.c
    gx/texture.c
    hsd/model.c
    hsd/aobj.c
    hsd/anim.c
    hsd/parts.c
    hsd/light.c
    game/attributes.c
    extras/physics.c)
target_include_directories(melee PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(melee PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(melee PRIVATE PkgConfig::SDL2 OpenGL::GL m)
option(MELEE_SANITIZE "Enable memory and undefined behavior checks" OFF)
if(MELEE_SANITIZE)
  target_compile_options(melee PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(melee PRIVATE -fsanitize=address,undefined)
endif()
