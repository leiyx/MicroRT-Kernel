add_library(mrtk_kernel STATIC)

target_sources(mrtk_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_irq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_schedule.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_mem_heap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_mem_pool.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_sem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_ipc_obj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_ringbuffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_obj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_msg_queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_mail_box.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_system.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_printf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/mrtk_event.c

    # ${CMAKE_CURRENT_SOURCE_DIR}/Port/hw_stm32f407.S
)

# PRIVATE: 编译该库时使用 include 目录
# PUBLIC:  编译该库时使用，且任何链接该库的其他目标也会自动包含该目录
target_include_directories(mrtk_kernel
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Port
)

target_compile_definitions(mrtk_kernel PUBLIC MRTK_KERNEL_VERSION=0.1)

# 测试环境定义：如果启用了测试（BUILD_TESTING），内核库也定义 UNIT_TESTING
# 这样 mrtk_tick_get 等函数在内核库中就不会被编译，避免符号冲突
# 使用 PRIVATE 确保定义不影响链接该库的其他目标
if(BUILD_TESTING)
    message(STATUS "Building kernel library with UNIT_TESTING defined")
    target_compile_definitions(mrtk_kernel PRIVATE UNIT_TESTING)
else()
    message(STATUS "Building kernel library without UNIT_TESTING")
endif()