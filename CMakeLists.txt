cmake_minimum_required(VERSION 3.30)

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

# 检查是否已经从外部（命令行或上级文件）定义了该变量
if(NOT DEFINED MRTK_BUILD_MODE)
    # 仅在未定义时设置默认值为 "TEST"
    set(MRTK_BUILD_MODE "TEST" CACHE STRING "Build mode for MRTK")
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# 显式设置是否构建测试（在添加 Kernel 前设置）
# 根据父项目定义的 MRTK_BUILD_MODE 决定构建模式
if(MRTK_BUILD_MODE STREQUAL "TEST")
    set(BUILD_TESTING ON CACHE BOOL "Build test suite" FORCE)
    message(STATUS "MRTK: TEST mode - Unit tests enabled")

    # 启用测试功能ctest
    enable_testing()
elseif(MRTK_BUILD_MODE STREQUAL "EMBEDDED")
    set(BUILD_TESTING OFF CACHE BOOL "Build test suite" FORCE)
    message(STATUS "MRTK: EMBEDDED mode - Cross-compilation for STM32")
else()
    message(FATAL_ERROR "MRTK_BUILD_MODE must be 'EMBEDDED' or 'TEST', got: ${MRTK_BUILD_MODE}")
endif()

# 添加内核子目录（此时 BUILD_TESTING 已被正确设置）
add_subdirectory(Kernel)

# 根据 BUILD_TESTING 决定是否编译测试（由 MRTK_BUILD_MODE 控制）
if(BUILD_TESTING)
    add_subdirectory(Test)
endif()