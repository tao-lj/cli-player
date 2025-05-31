cmake_minimum_required(VERSION 3.10)

project(cli_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -Ofast -flto -march=native -funroll-loops")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_mixer REQUIRED SDL2_mixer)
pkg_check_modules(opencv4 REQUIRED opencv4)

include_directories(
    ${SDL2_mixer_INCLUDE_DIRS}
    ${opencv4_INCLUDE_DIRS}
)

link_directories(
    ${SDL2_mixer_LIBRARY_DIRS}
    ${opencv4_LIBRARY_DIRS}
)

file(GLOB SRCS *.cpp)

add_executable(cli-player ${SRCS})

target_link_libraries(cli-player
    ${SDL2_mixer_LIBRARIES}
    ${opencv4_LIBRARIES}
)

set_target_properties(cli-player PROPERTIES CLEAN_DIRECTORIES "CMakeFiles")

include_directories(${CMAKE_SOURCE_DIR})

