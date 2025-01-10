cmake_minimum_required(VERSION 3.10)  # 设置最低版本要求

project(cli_player)  # 项目名称

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 开启一些常用的编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -Ofast -flto -march=native -funroll-loops")

# 寻找 pkg-config 配置的库和头文件路径
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_mixer REQUIRED SDL2_mixer)
pkg_check_modules(opencv4 REQUIRED opencv4)

# 设置头文件目录
include_directories(
  ${SDL2_mixer_INCLUDE_DIRS}
  ${opencv4_INCLUDE_DIRS}
)

# 设置库目录
link_directories(
  ${SDL2_mixer_LIBRARY_DIRS}
  ${opencv4_LIBRARY_DIRS}
)

# 查找所有源代码文件
file(GLOB SRCS *.cpp)

# 设置输出目标文件
add_executable(cli-player ${SRCS})

# 链接库
target_link_libraries(cli-player
  ${SDL2_mixer_LIBRARIES}
  ${opencv4_LIBRARIES}
)

# 设置清理目标
set_target_properties(cli-player PROPERTIES CLEAN_DIRECTORIES "CMakeFiles")

# 其它常见的清理操作
include_directories(${CMAKE_SOURCE_DIR})

