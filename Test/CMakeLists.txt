# 引入 FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

# Windows 兼容性设置 (Mac 会安全忽略)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# 下载并加载
FetchContent_MakeAvailable(googletest)

# ==============================================================================
# 定义测试程序
# ==============================================================================
add_executable(mrtk_test_demo

    mrtk_mock_hw.cc
    mrtk_test_irq.cc
    mrtk_test_list.cc
    mrtk_test_schedule.cc
    mrtk_test_ringbuffer.cc
    mrtk_test_mem_heap.cc
    mrtk_test_mem_pool.cc
    mrtk_test_timer.cc

    mrtk_test_task.cc
    mrtk_test_event.cc
    mrtk_test_mutex.cc
    mrtk_test_sem.cc
    mrtk_test_mail_box.cc
    mrtk_test_msg_queue.cc
)

# ==============================================================================
# 编译选项和链接
# ==============================================================================

# 设置编译定义，用于屏蔽硬件寄存器操作
target_compile_definitions(mrtk_test_demo PRIVATE UNIT_TESTING)

# 链接 GTest/GMock 库
# GTest::gmock_main 会自动提供一个包含 main() 的入口，你不需要自己写 main
target_link_libraries(mrtk_test_demo mrtk_kernel GTest::gmock_main)

# ==============================================================================
# CTest 集成
# ==============================================================================
include(GoogleTest)
gtest_discover_tests(mrtk_test_demo)

# ==============================================================================
# CMake 集成说明
# ==============================================================================
#
# 【构建与运行命令】
#
# 1. 配置项目（Debug 模式）：
# cmake -B build -DCMAKE_BUILD_TYPE=Debug
#
# 2. 编译项目：
# cmake --build build --config Debug
#
# 3. 运行所有测试：
# ctest --test-dir build --verbose
#
# 4. 运行单个测试可执行文件：
# cd build/Test && ./mrtk_test_demo.exe
#
# 5. 运行特定测试用例：
# cd build/Test && ./mrtk_test_demo.exe --gtest_filter="MemHeapTest.*"
# cd build/Test && ./mrtk_test_demo.exe --gtest_filter="MemPoolTest.*"
#
# 【测试文件说明】
#
# - mrtk_mock_hw.cc: 公共 Mock 层实现（CPU Port + Kernel API）
# - mrtk_mem_heap_test.cpp: 堆内存管理单元测试
# - mrtk_mem_pool_test.cpp: 内存池管理单元测试
#
# 【依赖说明】
#
# - Google Test/GMock: 通过 FetchContent 自动下载
# - mrtk_kernel: 内核库（由上级 CMakeLists.txt 提供）
#
# 【Mock 层架构】
#
# 所有测试模块共享公共 Mock 层：
# - MockCpuPort: 硬件中断、上下文切换 Mock
#
# 测试用例通过 mrtk_mock_set_cpu_port() 和 mrtk_mock_set_kernel()
# 注入 Mock 对象，在 TearDown() 中清除。
#
# ==============================================================================
