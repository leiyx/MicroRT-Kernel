add_library(mrtk_kernel STATIC)

target_sources(mrtk_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mrtk_task.c
)

# PRIVATE: 编译该库时使用 include 目录
# PUBLIC:  编译该库时使用，且任何链接该库的其他目标也会自动包含该目录
target_include_directories(mrtk_kernel
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(mrtk_kernel PUBLIC MRTK_KERNEL_VERSION=0.1)