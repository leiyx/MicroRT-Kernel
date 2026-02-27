# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME MicroRT-Kernel)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# 强制 CMake 导出编译命令数据库
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# 显式设置是否构建测试（在添加 Kernel 前设置）
if(CMAKE_CROSSCOMPILING)
    set(BUILD_TESTING OFF CACHE BOOL "Build test suite" FORCE)
    message(STATUS "Cross-compiling: Tests disabled")
else()
    set(BUILD_TESTING ON CACHE BOOL "Build test suite" FORCE)
    message(STATUS "Native build: Tests enabled")
    # 启用测试功能ctest
    enable_testing()
endif()

# 添加内核子目录（此时 BUILD_TESTING 已被正确设置）
add_subdirectory(Kernel)

if(CMAKE_CROSSCOMPILING)
else()
    # 本地编译DT，不使用交叉编译工具链
    add_subdirectory(Test)
endif()