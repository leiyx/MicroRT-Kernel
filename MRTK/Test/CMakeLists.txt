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

# 2. 定义测试程序
add_executable(mrtk_test_demo
    demo.cc
    mrtk_test_irq.cc
)

# 3. 设置编译定义，用于屏蔽硬件寄存器操作
target_compile_definitions(mrtk_test_demo PRIVATE UNIT_TESTING)

# 4. 链接 GTest 库
# GTest::gtest_main 会自动提供一个包含 main() 的入口，你不需要自己写 main
# GTest::gmock_main 在 gtest_main 上包含了更多东西
target_link_libraries(mrtk_test_demo mrtk_kernel GTest::gmock_main)

# 5. 让测试被 CTest 识别
include(GoogleTest)
gtest_discover_tests(mrtk_test_demo)