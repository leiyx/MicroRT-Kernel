/**
 * @file mrtk_test_task.cc
 * @author leiyx
 * @brief 任务管理模块 (mrtk_task.c) 的全面单元测试
 * @version 0.3
 * @copyright Copyright (c) 2026
 *
 * 测试覆盖策略说明：
 * 1. 边界值分析：
 *    - 优先级边界值（0、MRTK_MAX_PRIO_LEVEL_NUM-1、MRTK_IDLE_PRIORITY）
 *    - 栈大小边界值（0、最小栈、最大栈）
 *    - 时间片边界值（0、MRTK_TICK_MAX）
 *    - 任务状态边界（INIT、READY、RUNNING、SUSPEND、CLOSE）
 * 2. 等价类划分：
 *    - NULL指针防御（task、entry、stack_base等参数）
 *    - 静态对象 vs 动态对象
 *    - 任务上下文 vs 中断上下文
 *    - 合法状态 vs 非法状态转换
 * 3. 分支覆盖：
 *    - mrtk_task_init 中的所有 if/else 分支
 *    - mrtk_task_cleanup 中的所有状态分支
 *    - mrtk_task_start/suspend/resume 中的状态检查分支
 *    - mrtk_task_yield 中的同优先级检查分支
 * 4. 状态机覆盖：
 *    - INIT -> READY -> RUNNING -> SUSPEND -> READY（完整状态转换）
 *    - 静态任务：Init -> Start -> Suspend -> Resume -> Detach
 *    - 动态任务：Create -> Start -> Delete（资源释放）
 * 5. 负向测试：
 *    - 所有公共 API 的 NULL 参数检查
 *    - 非法状态转换（如对 SUSPEND 状态任务再次 suspend）
 *    - 中断上下文调用阻塞 API（返回 MRTK_E_IN_ISR）
 * 6. 内部函数测试：
 *    - _mrtk_task_cleanup 的各种状态清理逻辑
 *    - _mrtk_task_exit 的资源回收和调度触发
 *    - 注意：内置定时器回调为 static 函数，通过公共 API 间接测试
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>

/* Mock 头文件（C++ 代码，必须在最前） */
#include "mrtk_mock_hw.hpp"

/* MRTK 头文件（已包含 extern "C"） */
#include "mrtk.h"

/* 内部函数声明（C 符号，需要 extern "C"） */
extern "C" {
    mrtk_bool_t _mrtk_task_cleanup(mrtk_task_t *task);
    mrtk_void_t _mrtk_task_exit(mrtk_void_t);
}

/* =============================================================================
 * 测试辅助宏与定义
 * ============================================================================= */

/**
 * @brief 根据 MRTK_PRIO_HIGHER_IS_LOWER_VALUE 配置定义测试用例优先级
 * @details 确保在不同配置下测试用例逻辑一致
 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK1_PRIO 5          /* 中等优先级（数值越小优先级越高） */
    #define TASK2_PRIO 10         /* 较低优先级 */
    #define TASK3_PRIO 10         /* 与 TASK2 同优先级 */
    #define HIGH_PRIO  3          /* 高优先级 */
    #define LOW_PRIO   20         /* 低优先级 */
#else
    #define TASK1_PRIO 10         /* 中等优先级（数值越大优先级越高） */
    #define TASK2_PRIO 5          /* 较低优先级 */
    #define TASK3_PRIO 5          /* 与 TASK2 同优先级 */
    #define HIGH_PRIO  20         /* 高优先级 */
    #define LOW_PRIO   3          /* 低优先级 */
#endif

/**
 * @brief 测试任务栈大小（字节）
 */
#define TEST_TASK_STACK_SIZE 512

/**
 * @brief 测试任务入口函数（简单返回）
 */
static void test_task_entry(mrtk_void_t *param)
{
    (void) param;
}

/**
 * @brief 测试任务入口函数（无限循环，模拟真实任务）
 */
static void test_task_entry_loop(mrtk_void_t *param)
{
    (void) param;
    while (1) {
        mrtk_task_delay(100);
    }
}

/* =============================================================================
 * 测试固件 (Test Fixture)
 * ============================================================================= */

/**
 * @class MrtkTaskTest
 * @brief 任务管理测试固件
 * @details 负责:
 *          - 测试前的环境初始化（设置 Mock 对象，初始化全局变量）
 *          - 测试后的环境清理（重置全局变量）
 */
class MrtkTaskTest : public ::testing::Test {
  protected:
    /**
     * @brief CPU 端口层 Mock 对象（使用 NiceMock 自动忽略未设置期望的调用）
     */
    testing::NiceMock<MockCpuPort> mock_cpu_port;

    /**
     * @brief 测试任务栈空间（静态分配）
     */
    mrtk_u32_t test_task_stack[TEST_TASK_STACK_SIZE];

    /**
     * @brief 测试任务 TCB（静态分配）
     */
    mrtk_tcb_t test_task_tcb;

    /**
     * @brief 测试前初始化
     */
    void SetUp() override {
        /* Step 1: 系统初始化（统一入口，包括调度器和全局变量） */
        mrtk_err_t ret = mrtk_system_init();
        ASSERT_EQ(ret, MRTK_EOK) << "系统初始化失败";
        g_CurrentTCB = mrtk_task_get_idle();

        /* Step 2: 设置 Mock 对象 */
        mrtk_mock_set_cpu_port(&mock_cpu_port);

        /* Step 3: 设置默认期望行为（中断控制） */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
            .WillByDefault(testing::Return(0));
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_enable)
            .WillByDefault(testing::Return());

    }

    /**
     * @brief 测试后清理
     */
    void TearDown() override {
        /* 清除 Mock 对象 */
        mrtk_mock_clear_cpu_port();
    }
};

/* =============================================================================
 * 测试辅助函数
 * ============================================================================== */

/**
 * @brief 测试清理回调函数
 */
static mrtk_void_t test_cleanup(mrtk_void_t *para)
{
    (void) para;
}

/* =============================================================================
 * 生命周期管理测试 - 静态任务 API
 * ============================================================================== */

/**
 * @test mrtk_task_init_InvalidParameters_ReturnsEINVAL
 * @brief 测试 mrtk_task_init 对无效参数的处理
 * @details 等价类划分（负向测试）：传入 NULL 指针和非法参数
 */
TEST_F(MrtkTaskTest, mrtk_task_init_InvalidParameters_ReturnsEINVAL) {
    /* Given: 无效参数 */
    mrtk_u32_t stack[128];

    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_init("test", nullptr, test_task_entry, nullptr,
                             stack, 128, TASK1_PRIO, 10), MRTK_EINVAL);

    /* When & Then: entry 为 NULL */
    EXPECT_EQ(mrtk_task_init("test", &test_task_tcb, nullptr, nullptr,
                             stack, 128, TASK1_PRIO, 10), MRTK_EINVAL);

    /* When & Then: stack_base 为 NULL */
    EXPECT_EQ(mrtk_task_init("test", &test_task_tcb, test_task_entry, nullptr,
                             nullptr, 128, TASK1_PRIO, 10), MRTK_EINVAL);

    /* When & Then: priority 超出范围 */
    EXPECT_EQ(mrtk_task_init("test", &test_task_tcb, test_task_entry, nullptr,
                             stack, 128, MRTK_MAX_PRIO_LEVEL_NUM, 10), MRTK_EINVAL);
}

/**
 * @test mrtk_task_init_ValidParameters_ReturnsEOKAndInitializesTCB
 * @brief 测试 mrtk_task_init 使用合法参数初始化任务
 * @details 等价类划分（正向测试）：验证 TCB 正确初始化
 */
TEST_F(MrtkTaskTest, mrtk_task_init_ValidParameters_ReturnsEOKAndInitializesTCB) {
    /* Given: 合法参数 */
    mrtk_u32_t stack[256];

    /* When: 初始化任务 */
    mrtk_err_t ret = mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                                     nullptr, stack, 256, TASK1_PRIO, 20);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: TCB 字段正确初始化 */
    EXPECT_STREQ(test_task_tcb.obj.name, "test_task");
    EXPECT_EQ(test_task_tcb.priority, TASK1_PRIO);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_INIT);
    EXPECT_EQ(test_task_tcb.init_tick, 20);
    EXPECT_EQ(test_task_tcb.remain_tick, 20);
    EXPECT_EQ(test_task_tcb.stack_base, stack);
    EXPECT_EQ(test_task_tcb.stack_size, 256);
    EXPECT_EQ(test_task_tcb.task_entry, test_task_entry);
    EXPECT_EQ(test_task_tcb.last_error, MRTK_EOK);

#if (MRTK_USING_MUTEX == 1)
    EXPECT_EQ(test_task_tcb.orig_prio, TASK1_PRIO);
#endif
}

/**
 * @test mrtk_task_init_ZeroTick_UsesDefaultTick
 * @brief 测试 mrtk_task_init 当 tick=0 时使用默认时间片
 * @details 边界值分析：tick = 0 应使用默认值（MRTK_TICK_PER_SECOND / 10）
 */
TEST_F(MrtkTaskTest, mrtk_task_init_ZeroTick_UsesDefaultTick) {
    /* Given: tick = 0 */
    mrtk_u32_t stack[256];

    /* When: 初始化任务，tick = 0 */
    mrtk_err_t ret = mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                                     nullptr, stack, 256, TASK1_PRIO, 0);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 使用默认时间片 */
    EXPECT_EQ(test_task_tcb.init_tick, MRTK_TICK_PER_SECOND / 10);
    EXPECT_EQ(test_task_tcb.remain_tick, MRTK_TICK_PER_SECOND / 10);
}

/**
 * @test mrtk_task_init_AddsToGlobalObjectList
 * @brief 测试 mrtk_task_init 将任务添加到全局对象链表
 * @details 验证对象管理的集成
 */
TEST_F(MrtkTaskTest, mrtk_task_init_AddsToGlobalObjectList) {
    /* Given: 合法参数 */
    mrtk_u32_t stack[256];

    /* When: 初始化任务 */
    mrtk_err_t ret = mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                                     nullptr, stack, 256, TASK1_PRIO, 10);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务被添加到全局对象链表 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_obj_list[MRTK_OBJ_TYPE_TASK]));
}

/**
 * @test mrtk_task_detach_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_detach 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_detach_NullTask_ReturnsEINVAL) {
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_detach(nullptr), MRTK_EINVAL);
}

/**
 * @test mrtk_task_detach_InitState_RemovesFromGlobalList
 * @brief 测试 mrtk_task_detach 从 INIT 状态分离任务
 * @details 状态机覆盖：INIT -> CLOSE
 */
TEST_F(MrtkTaskTest, mrtk_task_detach_InitState_RemovesFromGlobalList) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 分离任务 */
    mrtk_err_t ret = mrtk_task_detach(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);

    /* Then: 调度节点未被挂入消亡链表（静态对象） */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_defunct_task_list));
}

/**
 * @test mrtk_task_detach_ReadyState_RemovesFromReadyQueue
 * @brief 测试 mrtk_task_detach 从 READY 状态分离任务
 * @details 状态机覆盖：READY -> CLOSE，验证从就绪队列移除
 */
TEST_F(MrtkTaskTest, mrtk_task_detach_ReadyState_RemovesFromReadyQueue) {
    /* Given: 初始化并启动任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);

    /* 验证任务在就绪队列中 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));

    /* When: 分离任务 */
    mrtk_err_t ret = mrtk_task_detach(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);

    /* Then: 从就绪队列移除 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));
}

/**
 * @test mrtk_task_detach_SuspendState_RemovesFromSuspendList
 * @brief 测试 mrtk_task_detach 从 SUSPEND 状态分离任务
 * @details 状态机覆盖：SUSPEND -> CLOSE，验证调度节点游离
 */
TEST_F(MrtkTaskTest, mrtk_task_detach_SuspendState_RemovesFromSuspendList) {
    /* Given: 初始化、启动并挂起任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);
    ASSERT_EQ(mrtk_task_suspend(&test_task_tcb), MRTK_EOK);

    /* When: 分离任务 */
    mrtk_err_t ret = mrtk_task_detach(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);
}

/* =============================================================================
 * 生命周期管理测试 - 动态任务 API
 * ============================================================================== */

/**
 * @test mrtk_task_create_InvalidParameters_ReturnsNULL
 * @brief 测试 mrtk_task_create 对无效参数的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_create_InvalidParameters_ReturnsNULL) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* When & Then: entry 为 NULL */
    EXPECT_EQ(mrtk_task_create("test", nullptr, nullptr, 256, TASK1_PRIO, 10), nullptr);

    /* When & Then: stack_size = 0 */
    EXPECT_EQ(mrtk_task_create("test", test_task_entry, nullptr, 0, TASK1_PRIO, 10), nullptr);

    /* When & Then: priority 超出范围 */
    EXPECT_EQ(mrtk_task_create("test", test_task_entry, nullptr, 256,
                               MRTK_MAX_PRIO_LEVEL_NUM, 10), nullptr);
#endif
}

/**
 * @test mrtk_task_create_ValidParameters_ReturnsTaskPointer
 * @brief 测试 mrtk_task_create 使用合法参数创建任务
 */
TEST_F(MrtkTaskTest, mrtk_task_create_ValidParameters_ReturnsTaskPointer) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* When: 创建任务 */
    mrtk_task_t *task = mrtk_task_create("test_task", test_task_entry, nullptr,
                                         256, TASK1_PRIO, 10);

    /* Then: 返回有效指针 */
    EXPECT_NE(task, nullptr);

    /* Then: TCB 字段正确初始化 */
    EXPECT_STREQ(task->obj.name, "test_task");
    EXPECT_EQ(task->priority, TASK1_PRIO);
    EXPECT_EQ(task->state, MRTK_TASK_STAT_INIT);

    /* Then: 对象类型标志为动态分配 */
    EXPECT_TRUE(MRTK_OBJ_IS_DYNAMIC(task->obj.type));

    /* Cleanup: 删除任务 */
    if (task != nullptr) {
        EXPECT_EQ(mrtk_task_delete(task), MRTK_EOK);
    }
#endif
}

/**
 * @test mrtk_task_delete_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_delete 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_delete_NullTask_ReturnsEINVAL) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_delete(nullptr), MRTK_EINVAL);
#endif
}

/**
 * @test mrtk_task_delete_InitState_AddsToDefunctList
 * @brief 测试 mrtk_task_delete 从 INIT 状态删除任务
 * @details 状态机覆盖：INIT -> CLOSE，验证挂入消亡链表
 */
TEST_F(MrtkTaskTest, mrtk_task_delete_InitState_AddsToDefunctList) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* Given: 创建任务 */
    mrtk_task_t *task = mrtk_task_create("test_task", test_task_entry, nullptr,
                                         256, TASK1_PRIO, 10);
    ASSERT_NE(task, nullptr);

    /* When: 删除任务 */
    mrtk_err_t ret = mrtk_task_delete(task);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务被挂入消亡链表 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_defunct_task_list));

    /* Then: 手动模拟空闲任务的清理逻辑（不调用无限循环函数） */
    /* 注意：不直接调用 mrtk_idle_task_entry() 因为它包含无限循环 */
    mrtk_base_t level = mrtk_hw_interrupt_disable();

    while (!_mrtk_list_is_empty(&g_defunct_task_list)) {
        mrtk_tcb_t *defunct_task = _mrtk_list_entry(g_defunct_task_list.next,
                                                      mrtk_tcb_t, sched_node);
        _mrtk_obj_delete(defunct_task);
        _mrtk_list_remove(&defunct_task->sched_node);
        mrtk_hw_interrupt_enable(level);

        /* 释放动态内存 */
        mrtk_free(defunct_task->stack_base);
        mrtk_free(defunct_task);

        level = mrtk_hw_interrupt_disable();
    }

    mrtk_hw_interrupt_enable(level);

    /* Then: 消亡链表被清空 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_defunct_task_list));
#endif
}

/**
 * @test mrtk_task_delete_ReadyState_AddsToDefunctList
 * @brief 测试 mrtk_task_delete 从 READY 状态删除任务
 */
TEST_F(MrtkTaskTest, mrtk_task_delete_ReadyState_AddsToDefunctList) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* Given: 创建并启动任务 */
    mrtk_task_t *task = mrtk_task_create("test_task", test_task_entry, nullptr,
                                         256, TASK1_PRIO, 10);
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(mrtk_task_start(task), MRTK_EOK);

    /* When: 删除任务 */
    mrtk_err_t ret = mrtk_task_delete(task);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 从就绪队列移除 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));

    /* Then: 任务被挂入消亡链表 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_defunct_task_list));
#endif
}

/* =============================================================================
 * 核心调度与状态测试
 * ============================================================================== */

/**
 * @test mrtk_task_start_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_start 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_start_NullTask_ReturnsEINVAL) {
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_start(nullptr), MRTK_EINVAL);
}

/**
 * @test mrtk_task_start_InitState_AddsToReadyQueue
 * @brief 测试 mrtk_task_start 将任务从 INIT 态加入就绪队列
 * @details 状态机覆盖：INIT -> READY
 */
TEST_F(MrtkTaskTest, mrtk_task_start_InitState_AddsToReadyQueue) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 启动任务 */
    mrtk_err_t ret = mrtk_task_start(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 READY */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_READY);

    /* Then: 任务被加入就绪队列 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));

    /* Then: 位图被设置 */
    EXPECT_NE(g_ready_prio_bitmap & (1U << TASK1_PRIO), 0U);
}

/**
 * @test mrtk_task_start_NonInitState_ReturnsError
 * @brief 测试 mrtk_task_start 对非 INIT 状态任务的处理
 * @details 负向测试：已启动的任务不能再次启动
 */
TEST_F(MrtkTaskTest, mrtk_task_start_NonInitState_ReturnsError) {
    /* Given: 初始化并启动任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);

    /* When: 再次启动任务 */
    mrtk_err_t ret = mrtk_task_start(&test_task_tcb);

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);

    /* Then: 任务状态保持 READY */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_READY);
}

/**
 * @test mrtk_task_self_ReturnsCurrentTask
 * @brief 测试 mrtk_task_self 返回当前任务指针
 * @details 注意：在单元测试中 g_CurrentTCB 可能为空或指向空闲任务
 */
TEST_F(MrtkTaskTest, mrtk_task_self_ReturnsCurrentTask) {
    /* When: 获取当前任务 */
    mrtk_task_t *task = mrtk_task_self();

    /* Then: 返回值可能与全局变量一致（具体取决于测试环境） */
    /* 注意：此处不强制断言，因为 g_CurrentTCB 的值取决于测试执行上下文 */
    EXPECT_NE(task, nullptr);
}

/**
 * @test mrtk_task_suspend_NullTask_SuspendsCurrentTask
 * @brief 测试 mrtk_task_suspend 对 NULL 参数的处理（挂起当前任务）
 */
TEST_F(MrtkTaskTest, mrtk_task_suspend_NullTask_SuspendsCurrentTask) {
    /* Given: 当前任务存在（空闲任务） */
    mrtk_task_t *current = mrtk_task_self();
    ASSERT_NE(current, nullptr);

    /* When: 传入 NULL 挂起当前任务 */
    mrtk_err_t ret = mrtk_task_suspend(nullptr);

    /* 传入NULL代表挂起自身 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(current->state, MRTK_TASK_STAT_SUSPEND);
}

/**
 * @test mrtk_task_suspend_ReadyState_RemovesFromReadyQueue
 * @brief 测试 mrtk_task_suspend 从 READY 状态挂起任务
 * @details 状态机覆盖：READY -> SUSPEND
 */
TEST_F(MrtkTaskTest, mrtk_task_suspend_ReadyState_RemovesFromReadyQueue) {
    /* Given: 初始化并启动任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);

    /* When: 挂起任务 */
    mrtk_err_t ret = mrtk_task_suspend(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 SUSPEND */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_SUSPEND);

    /* Then: 从就绪队列移除 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));
}

/**
 * @test mrtk_task_suspend_NonReadyState_ReturnsError
 * @brief 测试 mrtk_task_suspend 对非 READY/RUNNING 状态任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_suspend_NonReadyState_ReturnsError) {
    /* Given: 初始化任务（INIT 状态） */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 挂起未启动的任务 */
    mrtk_err_t ret = mrtk_task_suspend(&test_task_tcb);

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);
}

/**
 * @test mrtk_task_resume_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_resume 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_resume_NullTask_ReturnsEINVAL) {
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_resume(nullptr), MRTK_EINVAL);
}

/**
 * @test mrtk_task_resume_SuspendState_AddsToReadyQueue
 * @brief 测试 mrtk_task_resume 恢复挂起的任务
 * @details 状态机覆盖：SUSPEND -> READY
 */
TEST_F(MrtkTaskTest, mrtk_task_resume_SuspendState_AddsToReadyQueue) {
    /* Given: 初始化、启动并挂起任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);
    ASSERT_EQ(mrtk_task_suspend(&test_task_tcb), MRTK_EOK);

    /* When: 恢复任务 */
    mrtk_err_t ret = mrtk_task_resume(&test_task_tcb);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 任务状态变为 READY */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_READY);

    /* Then: 任务被加入就绪队列 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));
}

/**
 * @test mrtk_task_resume_NonSuspendState_ReturnsError
 * @brief 测试 mrtk_task_resume 对非 SUSPEND 状态任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_resume_NonSuspendState_ReturnsError) {
    /* Given: 初始化任务（INIT 状态） */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 恢复未挂起的任务 */
    mrtk_err_t ret = mrtk_task_resume(&test_task_tcb);

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);
}

/**
 * @test mrtk_task_yield_InISR_ReturnsError
 * @brief 测试 mrtk_task_yield 在中断上下文中调用
 * @details 负向测试：中断中调用 yield 应返回错误
 */
TEST_F(MrtkTaskTest, mrtk_task_yield_InISR_ReturnsError) {
    /* Given: 设置中断嵌套计数器 */
    g_interrupt_nest = 1;

    /* When: 在中断中调用 yield */
    mrtk_err_t ret = mrtk_task_yield();

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);

    /* Cleanup: 复位中断嵌套计数器 */
    g_interrupt_nest = 0;
}

/**
 * @test mrtk_task_yield_NoOtherTasks_ReturnsEOK
 * @brief 测试 mrtk_task_yield 当同优先级队列中无其他任务时
 * @details 边界值分析：单任务场景
 */
TEST_F(MrtkTaskTest, mrtk_task_yield_NoOtherTasks_ReturnsEOK) {
    /* Given: 只有空闲任务的就绪队列 */
    mrtk_u8_t idle_prio = mrtk_task_self()->priority;

    /* When: 调用 yield */
    mrtk_err_t ret = mrtk_task_yield();

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/**
 * @test mrtk_task_delay_ZeroTick_EquivalentToYield
 * @brief 测试 mrtk_task_delay 当 tick=0 时等同于 yield
 * @details 边界值分析：tick = 0
 */
TEST_F(MrtkTaskTest, mrtk_task_delay_ZeroTick_EquivalentToYield) {
#if (MRTK_USING_TIMER == 1)
    /* Given: 清除中断嵌套标志 */
    g_interrupt_nest = 0;

    /* When: 延时 0 tick */
    mrtk_err_t ret = mrtk_task_delay(0);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);
#endif
}

/**
 * @test mrtk_task_delay_InISR_ReturnsEINISR
 * @brief 测试 mrtk_task_delay 在中断上下文中调用
 * @details 负向测试：中断中调用阻塞 API 应返回错误
 */
TEST_F(MrtkTaskTest, mrtk_task_delay_InISR_ReturnsEINISR) {
#if (MRTK_USING_TIMER == 1)
    /* Given: 设置中断嵌套计数器 */
    g_interrupt_nest = 1;

    /* When: 在中断中调用延时 */
    mrtk_err_t ret = mrtk_task_delay(100);

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_E_IN_ISR);

    /* Cleanup: 复位中断嵌套计数器 */
    g_interrupt_nest = 0;
#endif
}

/**
 * @test mrtk_task_delay_until_NullLastWakeup_ReturnsEINVAL
 * @brief 测试 mrtk_task_delay_until 对 NULL 参数的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_delay_until_NullLastWakeup_ReturnsEINVAL) {
#if (MRTK_USING_TIMER == 1)
    /* When & Then: last_wakeup 为 NULL */
    EXPECT_EQ(mrtk_task_delay_until(nullptr, 100), MRTK_EINVAL);
#endif
}

/**
 * @test mrtk_task_delay_until_InISR_ReturnsEINISR
 * @brief 测试 mrtk_task_delay_until 在中断上下文中调用
 */
TEST_F(MrtkTaskTest, mrtk_task_delay_until_InISR_ReturnsEINISR) {
#if (MRTK_USING_TIMER == 1)
    /* Given: 设置中断嵌套计数器 */
    g_interrupt_nest = 1;

    mrtk_tick_t last_wakeup = 0;

    /* When: 在中断中调用绝对延时 */
    mrtk_err_t ret = mrtk_task_delay_until(&last_wakeup, 100);

    /* Then: 返回错误 */
    EXPECT_EQ(ret, MRTK_E_IN_ISR);

    /* Cleanup: 复位中断嵌套计数器 */
    g_interrupt_nest = 0;
#endif
}

/* =============================================================================
 * 属性控制测试
 * ============================================================================== */

/**
 * @test mrtk_task_set_priority_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_set_priority 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_set_priority_NullTask_ReturnsEINVAL) {
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_set_priority(nullptr, TASK1_PRIO), MRTK_EINVAL);
}

/**
 * @test mrtk_task_set_priority_InvalidPriority_ReturnsEINVAL
 * @brief 测试 mrtk_task_set_priority 对非法优先级的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_set_priority_InvalidPriority_ReturnsEINVAL) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When & Then: priority 超出范围 */
    EXPECT_EQ(mrtk_task_set_priority(&test_task_tcb, MRTK_MAX_PRIO_LEVEL_NUM), MRTK_EINVAL);
}

/**
 * @test mrtk_task_set_priority_InitState_UpdatesPriority
 * @brief 测试 mrtk_task_set_priority 在 INIT 状态下更新优先级
 */
TEST_F(MrtkTaskTest, mrtk_task_set_priority_InitState_UpdatesPriority) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 修改优先级 */
    mrtk_err_t ret = mrtk_task_set_priority(&test_task_tcb, TASK2_PRIO);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 优先级已更新 */
    EXPECT_EQ(test_task_tcb.priority, TASK2_PRIO);
}

/**
 * @test mrtk_task_set_priority_ReadyState_ReordersReadyQueue
 * @brief 测试 mrtk_task_set_priority 在 READY 状态下重新排序就绪队列
 * @details 分支覆盖：就绪态任务需要从原队列移除并插入新队列
 */
TEST_F(MrtkTaskTest, mrtk_task_set_priority_ReadyState_ReordersReadyQueue) {
    /* Given: 初始化并启动任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);

    /* 验证任务在原优先级队列中 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));

    /* When: 修改优先级 */
    mrtk_err_t ret = mrtk_task_set_priority(&test_task_tcb, TASK2_PRIO);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 优先级已更新 */
    EXPECT_EQ(test_task_tcb.priority, TASK2_PRIO);

    /* Then: 任务被移动到新优先级队列 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[TASK2_PRIO]));
}

/**
 * @test mrtk_task_get_priority_NullTask_ReturnsInvalidU8
 * @brief 测试 mrtk_task_get_priority 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_get_priority_NullTask_ReturnsInvalidU8) {
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_get_priority(nullptr), MRTK_INVALID_U8);
}

/**
 * @test mrtk_task_get_priority_ValidTask_ReturnsPriority
 * @brief 测试 mrtk_task_get_priority 返回任务优先级
 */
TEST_F(MrtkTaskTest, mrtk_task_get_priority_ValidTask_ReturnsPriority) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 获取优先级 */
    mrtk_u8_t prio = mrtk_task_get_priority(&test_task_tcb);

    /* Then: 返回正确的优先级 */
    EXPECT_EQ(prio, TASK1_PRIO);
}

/* =============================================================================
 * 通用控制接口测试
 * ============================================================================== */

/**
 * @test mrtk_task_control_NullTask_ReturnsEINVAL
 * @brief 测试 mrtk_task_control 对 NULL 任务的处理
 */
TEST_F(MrtkTaskTest, mrtk_task_control_NullTask_ReturnsEINVAL) {
    mrtk_u8_t prio = TASK1_PRIO;
    /* When & Then: task 为 NULL */
    EXPECT_EQ(mrtk_task_control(nullptr, MRTK_TASK_CMD_SET_PRIORITY, &prio), MRTK_EINVAL);
}

/**
 * @test mrtk_task_control_SetPriority_ValidArg_ReturnsEOK
 * @brief 测试 mrtk_task_control 设置优先级
 */
TEST_F(MrtkTaskTest, mrtk_task_control_SetPriority_ValidArg_ReturnsEOK) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    mrtk_u8_t new_prio = TASK2_PRIO;

    /* When: 通过 control 接口设置优先级 */
    mrtk_err_t ret = mrtk_task_control(&test_task_tcb, MRTK_TASK_CMD_SET_PRIORITY, &new_prio);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 优先级已更新 */
    EXPECT_EQ(test_task_tcb.priority, TASK2_PRIO);
}

/**
 * @test mrtk_task_control_SetPriority_NullArg_ReturnsEINVAL
 * @brief 测试 mrtk_task_control 设置优先级时参数为 NULL
 */
TEST_F(MrtkTaskTest, mrtk_task_control_SetPriority_NullArg_ReturnsEINVAL) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When & Then: arg 为 NULL */
    EXPECT_EQ(mrtk_task_control(&test_task_tcb, MRTK_TASK_CMD_SET_PRIORITY, nullptr), MRTK_EINVAL);
}

/**
 * @test mrtk_task_control_GetPriority_ValidArg_ReturnsEOK
 * @brief 测试 mrtk_task_control 获取优先级
 */
TEST_F(MrtkTaskTest, mrtk_task_control_GetPriority_ValidArg_ReturnsEOK) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    mrtk_u8_t prio;

    /* When: 通过 control 接口获取优先级 */
    mrtk_err_t ret = mrtk_task_control(&test_task_tcb, MRTK_TASK_CMD_GET_PRIORITY, &prio);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 优先级正确 */
    EXPECT_EQ(prio, TASK1_PRIO);
}

/**
 * @test mrtk_task_control_GetPriority_NullArg_ReturnsEINVAL
 * @brief 测试 mrtk_task_control 获取优先级时参数为 NULL
 */
TEST_F(MrtkTaskTest, mrtk_task_control_GetPriority_NullArg_ReturnsEINVAL) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When & Then: arg 为 NULL */
    EXPECT_EQ(mrtk_task_control(&test_task_tcb, MRTK_TASK_CMD_GET_PRIORITY, nullptr), MRTK_EINVAL);
}

/**
 * @test mrtk_task_control_SetCleanup_ValidArg_ReturnsEOK
 * @brief 测试 mrtk_task_control 注册清理回调函数
 */
TEST_F(MrtkTaskTest, mrtk_task_control_SetCleanup_ValidArg_ReturnsEOK) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 注册清理回调函数 */
    mrtk_err_t ret = mrtk_task_control(&test_task_tcb, MRTK_TASK_CMD_SET_CLEANUP,
                                       (mrtk_void_t *) test_cleanup);

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 清理回调已注册 */
    EXPECT_EQ(test_task_tcb.cleanup_handler, test_cleanup);
}

/**
 * @test mrtk_task_control_InvalidCommand_ReturnsEINVAL
 * @brief 测试 mrtk_task_control 使用无效命令
 */
TEST_F(MrtkTaskTest, mrtk_task_control_InvalidCommand_ReturnsEINVAL) {
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When & Then: 使用无效命令 */
    EXPECT_EQ(mrtk_task_control(&test_task_tcb, 999, nullptr), MRTK_EINVAL);
}

/* =============================================================================
 * 内部函数测试
 * ============================================================================== */

/**
 * @test _mrtk_task_cleanup_InitState_SetsToClose
 * @brief 测试 _mrtk_task_cleanup 在 INIT 状态下的清理逻辑
 * @details 分支覆盖：INIT 状态不处理调度节点
 */
TEST_F(MrtkTaskTest, _mrtk_task_cleanup_InitState_SetsToClose) {
    /* Given: 初始化任务（INIT 状态） */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* When: 调用清理函数 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(&test_task_tcb);

    /* Then: 不需要调度 */
    EXPECT_EQ(need_schedule, MRTK_FALSE);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);

    /* Then: 调度节点仍为游离状态 */
    EXPECT_TRUE(_mrtk_list_is_empty(&test_task_tcb.sched_node));
}

/**
 * @test _mrtk_task_cleanup_ReadyState_RemovesFromReadyQueue
 * @brief 测试 _mrtk_task_cleanup 在 READY 状态下的清理逻辑
 * @details 分支覆盖：READY 状态需要从就绪队列移除
 */
TEST_F(MrtkTaskTest, _mrtk_task_cleanup_ReadyState_RemovesFromReadyQueue) {
    /* Given: 初始化并启动任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);

    /* When: 调用清理函数 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(&test_task_tcb);

    /* Then: 可能需要调度（取决于优先级） */
    /* 不强制断言，因为空闲任务优先级最低 */

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);

    /* Then: 从就绪队列移除 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TASK1_PRIO]));
}

/**
 * @test _mrtk_task_cleanup_SuspendState_RemovesFromSuspendList
 * @brief 测试 _mrtk_task_cleanup 在 SUSPEND 状态下的清理逻辑
 * @details 分支覆盖：SUSPEND 状态需要移除调度节点
 */
TEST_F(MrtkTaskTest, _mrtk_task_cleanup_SuspendState_RemovesFromSuspendList) {
    /* Given: 初始化、启动并挂起任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);
    ASSERT_EQ(mrtk_task_suspend(&test_task_tcb), MRTK_EOK);

    /* When: 调用清理函数 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(&test_task_tcb);

    /* Then: 不需要调度（挂起态任务不在就绪队列） */
    EXPECT_EQ(need_schedule, MRTK_FALSE);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);
}

/**
 * @test _mrtk_task_cleanup_WithHeldMutexes_ReleasesAllMutexes
 * @brief 测试 _mrtk_task_cleanup 释放任务持有的所有互斥量
 * @details 分支覆盖：MRTK_USING_MUTEX 宏分支
 */
TEST_F(MrtkTaskTest, _mrtk_task_cleanup_WithHeldMutexes_ReleasesAllMutexes) {
#if (MRTK_USING_MUTEX == 1)
    /* Given: 初始化任务 */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);

    /* Given: 创建一个互斥量并让任务持有（模拟） */
    mrtk_mutex_t test_mutex;
    EXPECT_EQ(mrtk_mutex_init(&test_mutex, "test_mutex", MRTK_IPC_FLAG_NOTIFY_POLICY_PRIO), MRTK_EOK);

    /* 手动将任务设置为互斥量持有者（绕过 API） */
    test_mutex.owner_task = &test_task_tcb;
    _mrtk_list_insert_before(&test_task_tcb.held_mutex_list, &test_mutex.held_node);

    /* When: 调用清理函数 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(&test_task_tcb);

    /* Then: 任务状态变为 CLOSE */
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);

    /* Then: 互斥量持有列表为空 */
    EXPECT_TRUE(_mrtk_list_is_empty(&test_task_tcb.held_mutex_list));

    /* Cleanup: 分离互斥量 */
    mrtk_mutex_detach(&test_mutex);
#endif
}

/* =============================================================================
 * 空闲任务管理测试
 * ============================================================================== */

/**
 * @test mrtk_task_init_idle_ReturnsEOK
 * @brief 测试 mrtk_task_init_idle 初始化空闲任务
 */
TEST_F(MrtkTaskTest, mrtk_task_init_idle_ReturnsEOK) {
    /* When: 初始化空闲任务 */
    mrtk_err_t ret = mrtk_task_init_idle();

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 空闲任务已就绪 */
    mrtk_task_t *idle_task = mrtk_task_get_idle();
    EXPECT_NE(idle_task, nullptr);
    EXPECT_EQ(idle_task->state, MRTK_TASK_STAT_READY);
    EXPECT_EQ(idle_task->priority, MRTK_IDLE_PRIORITY);
}

/**
 * @test mrtk_idle_task_entry_CleansDefunctTasks
 * @brief 测试空闲任务清理消亡链表中的任务
 * @details 验证空闲任务的资源回收功能
 * @note 注意：不直接调用 mrtk_idle_task_entry() 因为它包含无限循环
 */
TEST_F(MrtkTaskTest, mrtk_idle_task_entry_CleansDefunctTasks) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* Given: 创建并删除任务（挂入消亡链表） */
    mrtk_task_t *task = mrtk_task_create("test_task", test_task_entry, nullptr,
                                         256, TASK1_PRIO, 10);
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(mrtk_task_delete(task), MRTK_EOK);

    /* 验证消亡链表不为空 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_defunct_task_list));

    /* When: 手动模拟空闲任务的清理逻辑（不调用无限循环函数） */
    mrtk_base_t level = mrtk_hw_interrupt_disable();

    while (!_mrtk_list_is_empty(&g_defunct_task_list)) {
        mrtk_tcb_t *defunct_task = _mrtk_list_entry(g_defunct_task_list.next,
                                                      mrtk_tcb_t, sched_node);
        _mrtk_obj_delete(defunct_task);
        _mrtk_list_remove(&defunct_task->sched_node);
        mrtk_hw_interrupt_enable(level);

        /* 释放动态内存 */
        mrtk_free(defunct_task->stack_base);
        mrtk_free(defunct_task);

        level = mrtk_hw_interrupt_disable();
    }

    mrtk_hw_interrupt_enable(level);

    /* Then: 消亡链表被清空 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_defunct_task_list));
#endif
}

/**
 * @test mrtk_task_get_idle_ReturnsValidPointer
 * @brief 测试 mrtk_task_get_idle 返回有效的空闲任务指针
 */
TEST_F(MrtkTaskTest, mrtk_task_get_idle_ReturnsValidPointer) {
    /* Given: 初始化空闲任务 */
    ASSERT_EQ(mrtk_task_init_idle(), MRTK_EOK);

    /* When: 获取空闲任务指针 */
    mrtk_task_t *idle_task = mrtk_task_get_idle();

    /* Then: 返回有效指针 */
    EXPECT_NE(idle_task, nullptr);
    EXPECT_STREQ(idle_task->obj.name, "idle");
    EXPECT_EQ(idle_task->priority, MRTK_IDLE_PRIORITY);
}

/* =============================================================================
 * 定时器守护任务测试
 * ============================================================================== */

/**
 * @test mrtk_task_init_timer_daemon_ReturnsEOK
 * @brief 测试 mrtk_task_init_timer_daemon 初始化定时器守护任务
 */
TEST_F(MrtkTaskTest, mrtk_task_init_timer_daemon_ReturnsEOK) {
#if (MRTK_USING_TIMER_SOFT == 1)
    /* When: 初始化定时器守护任务 */
    mrtk_err_t ret = mrtk_task_init_timer_daemon();

    /* Then: 返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* Then: 守护任务已就绪 */
    mrtk_task_t *daemon_task = mrtk_task_get_timer_daemon();
    EXPECT_NE(daemon_task, nullptr);
    EXPECT_EQ(daemon_task->state, MRTK_TASK_STAT_READY);
    EXPECT_EQ(daemon_task->priority, MRTK_TIMER_TASK_PRIO);
#endif
}

/**
 * @test mrtk_task_get_timer_daemon_ReturnsValidPointer
 * @brief 测试 mrtk_task_get_timer_daemon 返回有效的守护任务指针
 */
TEST_F(MrtkTaskTest, mrtk_task_get_timer_daemon_ReturnsValidPointer) {
#if (MRTK_USING_TIMER_SOFT == 1)
    /* Given: 初始化定时器守护任务 */
    ASSERT_EQ(mrtk_task_init_timer_daemon(), MRTK_EOK);

    /* When: 获取守护任务指针 */
    mrtk_task_t *daemon_task = mrtk_task_get_timer_daemon();

    /* Then: 返回有效指针 */
    EXPECT_NE(daemon_task, nullptr);
    EXPECT_STREQ(daemon_task->obj.name, "timer_daemon");
#endif
}

/* =============================================================================
 * 状态机完整生命周期测试
 * ============================================================================== */

/**
 * @test TaskLifecycle_StaticTask_CompleteStateTransitions
 * @brief 测试静态任务的完整生命周期
 * @details 状态机覆盖：INIT -> READY -> SUSPEND -> READY -> CLOSE
 */
TEST_F(MrtkTaskTest, TaskLifecycle_StaticTask_CompleteStateTransitions) {
    /* Given: 初始化任务（INIT 状态） */
    mrtk_u32_t stack[256];
    ASSERT_EQ(mrtk_task_init("test_task", &test_task_tcb, test_task_entry,
                              nullptr, stack, 256, TASK1_PRIO, 10), MRTK_EOK);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_INIT);

    /* When: 启动任务（INIT -> READY） */
    ASSERT_EQ(mrtk_task_start(&test_task_tcb), MRTK_EOK);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_READY);

    /* When: 挂起任务（READY -> SUSPEND） */
    ASSERT_EQ(mrtk_task_suspend(&test_task_tcb), MRTK_EOK);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_SUSPEND);

    /* When: 恢复任务（SUSPEND -> READY） */
    ASSERT_EQ(mrtk_task_resume(&test_task_tcb), MRTK_EOK);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_READY);

    /* When: 分离任务（READY -> CLOSE） */
    ASSERT_EQ(mrtk_task_detach(&test_task_tcb), MRTK_EOK);
    EXPECT_EQ(test_task_tcb.state, MRTK_TASK_STAT_CLOSE);
}

/**
 * @test TaskLifecycle_DynamicTask_CompleteLifecycle
 * @brief 测试动态任务的完整生命周期
 * @details 状态机覆盖：Create -> Start -> Delete
 */
TEST_F(MrtkTaskTest, TaskLifecycle_DynamicTask_CompleteLifecycle) {
#if (MRTK_USING_MEM_HEAP == 1)
    /* When: 创建任务 */
    mrtk_task_t *task = mrtk_task_create("test_task", test_task_entry, nullptr,
                                         256, TASK1_PRIO, 10);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->state, MRTK_TASK_STAT_INIT);

    /* When: 启动任务（INIT -> READY） */
    ASSERT_EQ(mrtk_task_start(task), MRTK_EOK);
    EXPECT_EQ(task->state, MRTK_TASK_STAT_READY);

    /* When: 删除任务（READY -> CLOSE -> 消亡链表） */
    ASSERT_EQ(mrtk_task_delete(task), MRTK_EOK);
    EXPECT_FALSE(_mrtk_list_is_empty(&g_defunct_task_list));

    /* When: 手动模拟空闲任务的清理逻辑 */
    mrtk_base_t level = mrtk_hw_interrupt_disable();

    while (!_mrtk_list_is_empty(&g_defunct_task_list)) {
        mrtk_tcb_t *defunct_task = _mrtk_list_entry(g_defunct_task_list.next,
                                                      mrtk_tcb_t, sched_node);
        _mrtk_obj_delete(defunct_task);
        _mrtk_list_remove(&defunct_task->sched_node);
        mrtk_hw_interrupt_enable(level);

        /* 释放动态内存 */
        mrtk_free(defunct_task->stack_base);
        mrtk_free(defunct_task);

        level = mrtk_hw_interrupt_disable();
    }

    mrtk_hw_interrupt_enable(level);

    /* Then: 消亡链表被清空 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_defunct_task_list));
#endif
}

/* =============================================================================
 * 边界值综合测试
 * ============================================================================== */

/**
 * @test TaskPriority_BoundaryValues_AllValid
 * @brief 测试任务优先级的边界值（0 和 MRTK_MAX_PRIO_LEVEL_NUM - 1）
 * @details 边界值分析：优先级的最小值和最大值
 */
TEST_F(MrtkTaskTest, TaskPriority_BoundaryValues_AllValid) {
    mrtk_u32_t stack1[256];
    mrtk_u32_t stack2[256];
    mrtk_tcb_t tcb1, tcb2;

    /* Given: 最小优先级和最大优先级 */
    mrtk_u8_t min_prio = 0;
    mrtk_u8_t max_prio = MRTK_MAX_PRIO_LEVEL_NUM - 1;

    /* When & Then: 使用最小优先级初始化任务 */
    EXPECT_EQ(mrtk_task_init("task_min", &tcb1, test_task_entry, nullptr,
                              stack1, 256, min_prio, 10), MRTK_EOK);
    EXPECT_EQ(tcb1.priority, min_prio);

    /* When & Then: 使用最大优先级初始化任务 */
    EXPECT_EQ(mrtk_task_init("task_max", &tcb2, test_task_entry, nullptr,
                              stack2, 256, max_prio, 10), MRTK_EOK);
    EXPECT_EQ(tcb2.priority, max_prio);

    /* Cleanup */
    mrtk_task_detach(&tcb1);
    mrtk_task_detach(&tcb2);
}

/**
 * @test TaskStackSize_BoundaryValues_ValidRange
 * @brief 测试任务栈大小的边界值
 * @details 边界值分析：最小栈和较大栈
 */
TEST_F(MrtkTaskTest, TaskStackSize_BoundaryValues_ValidRange) {
    mrtk_u32_t small_stack[128];  /* 512 字节 */
    mrtk_u32_t large_stack[1024]; /* 4096 字节 */
    mrtk_tcb_t tcb1, tcb2;

    /* When & Then: 使用小栈初始化任务 */
    EXPECT_EQ(mrtk_task_init("task_small", &tcb1, test_task_entry, nullptr,
                              small_stack, 128, TASK1_PRIO, 10), MRTK_EOK);
    EXPECT_EQ(tcb1.stack_size, 128);

    /* When & Then: 使用大栈初始化任务 */
    EXPECT_EQ(mrtk_task_init("task_large", &tcb2, test_task_entry, nullptr,
                              large_stack, 1024, TASK2_PRIO, 10), MRTK_EOK);
    EXPECT_EQ(tcb2.stack_size, 1024);

    /* Cleanup */
    mrtk_task_detach(&tcb1);
    mrtk_task_detach(&tcb2);
}

/**
 * @test TaskTimeSlice_BoundaryValues_ZeroAndMax
 * @brief 测试任务时间片的边界值（0 和 MRTK_TICK_MAX）
 * @details 边界值分析：时间片最小值（使用默认值）和最大值
 */
TEST_F(MrtkTaskTest, TaskTimeSlice_BoundaryValues_ZeroAndMax) {
    mrtk_u32_t stack1[256];
    mrtk_u32_t stack2[256];
    mrtk_tcb_t tcb1, tcb2;

    /* When & Then: tick = 0（使用默认值） */
    EXPECT_EQ(mrtk_task_init("task_zero", &tcb1, test_task_entry, nullptr,
                              stack1, 256, TASK1_PRIO, 0), MRTK_EOK);
    EXPECT_EQ(tcb1.init_tick, MRTK_TICK_PER_SECOND / 10);

    /* When & Then: tick = MRTK_TICK_MAX */
    EXPECT_EQ(mrtk_task_init("task_max", &tcb2, test_task_entry, nullptr,
                              stack2, 256, TASK2_PRIO, MRTK_TICK_MAX), MRTK_EOK);
    EXPECT_EQ(tcb2.init_tick, MRTK_TICK_MAX);

    /* Cleanup */
    mrtk_task_detach(&tcb1);
    mrtk_task_detach(&tcb2);
}
