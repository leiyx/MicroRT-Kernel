/**
 * @file mrtk_test_schedule.cc
 * @author leiyx
 * @brief 调度器模块 (mrtk_schedule.c) 的全面单元测试
 * @version 0.3
 * @copyright Copyright (c) 2026
 *
 * 测试覆盖策略说明：
 * 1. 边界值分析：
 *    - 优先级边界值（0、MRTK_MAX_PRIO_LEVEL_NUM-1、MRTK_IDLE_PRIORITY）
 *    - 位图边界（全0、全1、单个位）
 *    - 调度锁嵌套（0、1、最大嵌套）
 *    - 中断嵌套（0、1、多层嵌套）
 * 2. 等价类划分：
 *    - 空就绪队列 vs 非空就绪队列
 *    - 单任务 vs 多任务 vs 同优先级多任务
 *    - 任务上下文 vs 中断上下文
 *    - 锁定状态 vs 非锁定状态
 * 3. 分支覆盖：
 *    - mrtk_schedule 中的所有 if/else 分支
 *    - 位图查找的所有分支（低字节、高字节、CLZ 指令）
 *    - 调度锁嵌套的所有分支
 * 4. 状态机覆盖：
 *    - 初始化 -> 插入 -> 调度 -> 移除 -> 锁定/解锁
 *    - 空闲任务就绪场景
 * 5. 负向测试：
 *    - 所有公共 API 不接受 NULL 参数（内部函数假定参数有效）
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
    mrtk_u8_t _mrtk_schedule_get_highest_prio(mrtk_void_t);
}

/* =============================================================================
 * 测试固件 (Test Fixture)
 * ============================================================================= */

/**
 * @class MrtkScheduleTest
 * @brief 调度器测试固件
 * @details 负责:
 *          - 测试前的环境初始化（设置 Mock 对象，初始化全局变量）
 *          - 测试后的环境清理（重置全局变量）
 */
class MrtkScheduleTest : public ::testing::Test {
  protected:
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

        /* Step 3: 设置默认期望行为 */
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

    /* Mock 对象实例（使用 NiceMock 自动忽略未设置期望的调用） */
    testing::NiceMock<MockCpuPort> mock_cpu_port;

    /* 测试用的 TCB 数组 */
    mrtk_tcb_t test_tcbs[MRTK_MAX_PRIO_LEVEL_NUM];

    /**
     * @brief 辅助函数：初始化 TCB
     * @param tcb TCB 指针
     * @param priority 优先级
     */
    void InitTCB(mrtk_tcb_t *tcb, mrtk_u8_t priority) {
        memset(tcb, 0, sizeof(mrtk_tcb_t));
        tcb->priority = priority;
        tcb->state    = MRTK_TASK_STAT_READY;
        _mrtk_list_init(&tcb->sched_node);
    }

    /**
     * @brief 辅助函数：验证就绪队列的完整性
     * @param priority 优先级
     * @return mrtk_bool_t 完整返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t VerifyReadyQueueIntegrity(mrtk_u8_t priority) {
        mrtk_list_node_t *head = &g_ready_task_list[priority];

        /* 检查哨兵节点 */
        if (head->prev->next != head || head->next->prev != head) {
            return MRTK_FALSE;
        }

        return MRTK_TRUE;
    }

    /**
     * @brief 辅助函数：统计就绪队列中的任务数
     * @param priority 优先级
     * @return mrtk_u32_t 任务数
     */
    mrtk_u32_t CountTasksInReadyQueue(mrtk_u8_t priority) {
        return _mrtk_list_len(&g_ready_task_list[priority]);
    }

    /**
     * @brief 辅助函数：验证位图与实际就绪队列的一致性
     * @return mrtk_bool_t 一致返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t VerifyBitmapConsistency(void) {
        for (mrtk_u32_t i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
            mrtk_bool_t is_empty = _mrtk_list_is_empty(&g_ready_task_list[i]);
            mrtk_bool_t bit_set   = MRTK_GET_BIT_VAL(g_ready_prio_bitmap, i);

            if (is_empty && bit_set) {
                /* 队列为空但位图被设置 */
                return MRTK_FALSE;
            }
            if (!is_empty && !bit_set) {
                /* 队列非空但位图未被设置 */
                return MRTK_FALSE;
            }
        }

        return MRTK_TRUE;
    }
};

/* =============================================================================
 * 初始化测试
 * ============================================================================= */

/**
 * @test ScheduleInit_ValidInit_AllGlobalsReset
 * @brief 测试调度器初始化（状态机覆盖）
 * @details 验证初始化后所有全局变量都被正确复位，且系统任务已就绪
 * @note 系统初始化流程：
 *       1. mrtk_schedule_init() - 初始化调度器全局变量
 *       2. mrtk_task_init_idle() - 创建并启动空闲任务（优先级 MRTK_IDLE_PRIORITY）
 *       3. mrtk_task_init_timer_daemon() - 创建并启动定时器守护任务（优先级 MRTK_TIMER_TASK_PRIO）
 *       系统初始化后就绪队列中有两个系统任务：空闲任务 + 定时器守护任务
 */
TEST_F(MrtkScheduleTest, ScheduleInit_ValidInit_AllGlobalsReset) {
    /* Given: 系统已初始化 */

    /* Then: 验证空闲任务优先级的队列不为空（空闲任务已插入） */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[MRTK_IDLE_PRIORITY]));

#if (MRTK_USING_TIMER_SOFT == 1)
    /* Then: 验证定时器守护任务优先级的队列不为空（定时器守护任务已插入） */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[MRTK_TIMER_TASK_PRIO]));
#endif

    /* Then: 验证其他优先级队列为空 */
    for (mrtk_u32_t i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
        if (i != MRTK_IDLE_PRIORITY) {
#if (MRTK_USING_TIMER_SOFT == 1)
            if (i != MRTK_TIMER_TASK_PRIO) {
#endif
                EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[i]));
#if (MRTK_USING_TIMER_SOFT == 1)
            }
#endif
        }
    }

    /* Then: 验证位图中有两个位被设置（空闲任务 + 定时器守护任务） */
    mrtk_u32_t expected_bitmap = (1U << MRTK_IDLE_PRIORITY);
#if (MRTK_USING_TIMER_SOFT == 1)
    expected_bitmap |= (1U << MRTK_TIMER_TASK_PRIO);
#endif
    EXPECT_EQ(g_ready_prio_bitmap, expected_bitmap);

    /* Then: 验证调度锁嵌套计数器为 0 */
    EXPECT_EQ(g_schedule_lock_nest, 0U);

    /* Then: 验证延迟调度标志为 0 */
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);

    /* Then: 验证空闲任务可以被获取 */
    mrtk_task_t *idle_task = mrtk_task_get_idle();
    EXPECT_NE(idle_task, nullptr);
    EXPECT_EQ(idle_task->priority, MRTK_IDLE_PRIORITY);

#if (MRTK_USING_TIMER_SOFT == 1)
    /* Then: 验证定时器守护任务可以被获取 */
    mrtk_task_t *timer_daemon = mrtk_task_get_timer_daemon();
    EXPECT_NE(timer_daemon, nullptr);
    EXPECT_EQ(timer_daemon->priority, MRTK_TIMER_TASK_PRIO);
#endif
}

/* =============================================================================
 * _mrtk_schedule_get_highest_prio 测试
 * ============================================================================= */

/**
 * @test GetHighestPrio_OnlySystemTasks_ReturnTimerDaemonPriority
 * @brief 测试只有系统任务时返回定时器守护优先级（边界值分析）
 * @details 验证当只有系统任务（空闲任务 + 定时器守护任务）就绪时，
 *          返回定时器守护任务的优先级（高于空闲任务）
 * @note 系统初始化后默认就绪队列中有两个系统任务：
 *       - 空闲任务（优先级 MRTK_IDLE_PRIORITY，最低优先级）
 *       - 定时器守护任务（优先级 MRTK_TIMER_TASK_PRIO = 4，高于空闲任务）
 *       因此最高优先级应该是 MRTK_TIMER_TASK_PRIO（4）
 */
TEST_F(MrtkScheduleTest, GetHighestPrio_OnlySystemTasks_ReturnTimerDaemonPriority) {
    /* Given: 系统已初始化（只有系统任务：空闲任务 + 定时器守护任务） */

    /* When: 获取最高优先级 */
    mrtk_u8_t highest_prio = _mrtk_schedule_get_highest_prio();

    /* Then: 应该返回定时器守护任务优先级（高于空闲任务） */
#if (MRTK_USING_TIMER_SOFT == 1)
    EXPECT_EQ(highest_prio, MRTK_TIMER_TASK_PRIO);
#else
    /* 如果未启用软定时器，则只有空闲任务 */
    EXPECT_EQ(highest_prio, MRTK_IDLE_PRIORITY);
#endif
}

/**
 * @test GetHighestPrio_SingleTask_ReturnTaskPrio
 * @brief 测试单个用户任务场景（等价类划分）
 * @details 验证当有用户任务时，返回用户任务的优先级（而不是系统任务）
 * @note 系统初始化后已有两个系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能成为最高优先级任务
 */
TEST_F(MrtkScheduleTest, GetHighestPrio_SingleTask_ReturnTaskPrio) {
    /* Given: 系统已初始化（有系统任务：空闲任务 + 定时器守护任务） */

    /* Given: 添加一个用户任务（优先级高于定时器守护任务） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要 < MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 3
#else
    /* 优先级值越大优先级越高，需要 > MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* When: 获取最高优先级 */
    mrtk_u8_t highest_prio = _mrtk_schedule_get_highest_prio();

    /* Then: 应该返回用户任务的优先级（高于定时器守护任务和空闲任务） */
    EXPECT_EQ(highest_prio, TEST_PRIO);
#undef TEST_PRIO
}

/**
 * @test GetHighestPrio_MultipleTasks_ReturnHighest
 * @brief 测试多任务场景（分支覆盖）
 * @details 验证有多个就绪任务时，返回最高优先级
 */
TEST_F(MrtkScheduleTest, GetHighestPrio_MultipleTasks_ReturnHighest) {
    /* Given: 多个不同优先级的就绪任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK_HIGHEST_PRIO 3
    #define TASK_MID_PRIO     10
    #define TASK_LOW_PRIO     20
#else
    #define TASK_HIGHEST_PRIO 28
    #define TASK_MID_PRIO     15
    #define TASK_LOW_PRIO     5
#endif
    InitTCB(&test_tcbs[0], TASK_MID_PRIO);
    InitTCB(&test_tcbs[1], TASK_HIGHEST_PRIO);
    InitTCB(&test_tcbs[2], TASK_LOW_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* When: 获取最高优先级 */
    mrtk_u8_t highest_prio = _mrtk_schedule_get_highest_prio();

    /* Then: 应该返回最高优先级 */
    EXPECT_EQ(highest_prio, TASK_HIGHEST_PRIO);
#undef TASK_HIGHEST_PRIO
#undef TASK_MID_PRIO
#undef TASK_LOW_PRIO
}

/**
 * @test GetHighestPrio_AllPrioritiesNonEmpty_CoverAllBits
 * @brief 测试所有优先级都非空（边界值分析）
 * @details 验证当所有优先级位都被设置时，能正确找到最高优先级
 */
TEST_F(MrtkScheduleTest, GetHighestPrio_AllPrioritiesNonEmpty_CoverAllBits) {
    /* Given: 所有优先级都有任务 */
    for (mrtk_u32_t i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
        InitTCB(&test_tcbs[i], (mrtk_u8_t) i);
        _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[i]);
    }

    /* When: 获取最高优先级 */
    mrtk_u8_t highest_prio = _mrtk_schedule_get_highest_prio();

    /* Then: 应该返回 0 或 31（取决于配置） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    EXPECT_EQ(highest_prio, 0U);
#else
    EXPECT_EQ(highest_prio, (mrtk_u8_t) (MRTK_MAX_PRIO_LEVEL_NUM - 1));
#endif
}

/**
 * @test GetHighestPrio_AfterRemoveHighest_ReturnNextHighest
 * @brief 测试移除最高优先级任务后返回次高优先级（状态机覆盖）
 * @details 验证动态变化的就绪队列能正确响应
 * @note 系统初始化后已有定时器守护任务（优先级 4）
 *       用户任务的优先级需要高于定时器守护任务，才能测试移除后的次高优先级逻辑
 *       否则移除用户任务后，最高优先级仍然是定时器守护任务（4）
 */
TEST_F(MrtkScheduleTest, GetHighestPrio_AfterRemoveHighest_ReturnNextHighest) {
    /* Given: 系统已初始化（有定时器守护任务优先级 4） */

    /* Given: 添加多个用户任务（优先级都高于定时器守护任务） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要都 < MRTK_TIMER_TASK_PRIO (4) */
    #define TASK_HIGHEST_PRIO 1
    #define TASK_MID_PRIO     2
    #define TASK_LOW_PRIO     3
#else
    /* 优先级值越大优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define TASK_HIGHEST_PRIO 20
    #define TASK_MID_PRIO     15
    #define TASK_LOW_PRIO     10
#endif
    InitTCB(&test_tcbs[0], TASK_HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], TASK_MID_PRIO);
    InitTCB(&test_tcbs[2], TASK_LOW_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* When: 移除最高优先级任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 应该返回次高优先级（用户任务） */
    mrtk_u8_t highest_prio = _mrtk_schedule_get_highest_prio();
    EXPECT_EQ(highest_prio, TASK_MID_PRIO);
#undef TASK_HIGHEST_PRIO
#undef TASK_MID_PRIO
#undef TASK_LOW_PRIO
}

/* =============================================================================
 * _mrtk_schedule_insert_task 测试
 * ============================================================================= */

/**
 * @test InsertTask_SingleTask_Success
 * @brief 测试插入单个任务（状态机覆盖）
 * @details 验证任务被正确插入到对应优先级队列，位图被正确设置
 */
TEST_F(MrtkScheduleTest, InsertTask_SingleTask_Success) {
    /* Given: 初始化的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 7
#else
    #define TEST_PRIO 20
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);

    /* When: 插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* Then: 验证任务在队列中 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &g_ready_task_list[TEST_PRIO]);

    /* Then: 验证队列完整性 */
    EXPECT_TRUE(VerifyReadyQueueIntegrity(TEST_PRIO));

    /* Then: 验证位图一致性 */
    EXPECT_TRUE(VerifyBitmapConsistency());
#undef TEST_PRIO
}

/**
 * @test InsertTask_MultipleTasksSamePriority_FIFOOrder
 * @brief 测试插入多个同优先级任务（等价类划分）
 * @details 验证同优先级任务按 FIFO 顺序插入
 */
TEST_F(MrtkScheduleTest, InsertTask_MultipleTasksSamePriority_FIFOOrder) {
    /* Given: 多个同优先级任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 10
#else
    #define TEST_PRIO 15
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    InitTCB(&test_tcbs[1], TEST_PRIO);
    InitTCB(&test_tcbs[2], TEST_PRIO);

    /* When: 依次插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* Then: 验证队列顺序（FIFO） */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[1].sched_node);
    EXPECT_EQ(test_tcbs[1].sched_node.next, &test_tcbs[2].sched_node);
    EXPECT_EQ(test_tcbs[2].sched_node.next, &g_ready_task_list[TEST_PRIO]);

    /* Then: 验证任务数 */
    EXPECT_EQ(CountTasksInReadyQueue(TEST_PRIO), 3U);
#undef TEST_PRIO
}

/**
 * @test InsertTask_LowestPriority_Success
 * @brief 测试插入最低优先级任务（边界值分析）
 * @details 验证优先级为 0 或 MRTK_MAX_PRIO_LEVEL_NUM - 1 的任务能正常插入
 */
TEST_F(MrtkScheduleTest, InsertTask_LowestPriority_Success) {
    /* Given: 最低优先级任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO (MRTK_MAX_PRIO_LEVEL_NUM - 1)
#else
    #define TEST_PRIO 0
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);

    /* When: 插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* Then: 验证任务在队列中 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].prev, &test_tcbs[0].sched_node);
#undef TEST_PRIO
}

/**
 * @test InsertTask_HighestPriority_Success
 * @brief 测试插入最高优先级任务（边界值分析）
 * @details 验证优先级为 0 或 MRTK_MAX_PRIO_LEVEL_NUM - 1 的任务能正常插入
 */
TEST_F(MrtkScheduleTest, InsertTask_HighestPriority_Success) {
    /* Given: 最高优先级任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 0
#else
    #define TEST_PRIO (MRTK_MAX_PRIO_LEVEL_NUM - 1)
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);

    /* When: 插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* Then: 验证任务在队列中 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
#undef TEST_PRIO
}

/**
 * @test InsertTask_MultipleDifferentPriorities_AllInserted
 * @brief 测试插入多个不同优先级任务（等价类划分）
 * @details 验证不同优先级的任务被插入到正确的队列
 */
TEST_F(MrtkScheduleTest, InsertTask_MultipleDifferentPriorities_AllInserted) {
    /* Given: 多个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK1_PRIO 5
    #define TASK2_PRIO 15
    #define TASK3_PRIO 25
#else
    #define TASK1_PRIO 25
    #define TASK2_PRIO 15
    #define TASK3_PRIO 5
#endif
    InitTCB(&test_tcbs[0], TASK1_PRIO);
    InitTCB(&test_tcbs[1], TASK2_PRIO);
    InitTCB(&test_tcbs[2], TASK3_PRIO);

    /* When: 依次插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* Then: 验证所有优先级位图都被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK1_PRIO));
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK2_PRIO));
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK3_PRIO));

    /* Then: 验证每个任务都在正确的队列中 */
    EXPECT_EQ(g_ready_task_list[TASK1_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(g_ready_task_list[TASK2_PRIO].next, &test_tcbs[1].sched_node);
    EXPECT_EQ(g_ready_task_list[TASK3_PRIO].next, &test_tcbs[2].sched_node);
#undef TASK1_PRIO
#undef TASK2_PRIO
#undef TASK3_PRIO
}

/* =============================================================================
 * _mrtk_schedule_remove_task 测试
 * ============================================================================= */

/**
 * @test RemoveTask_SingleTask_BitmapCleared
 * @brief 测试移除单个任务（状态机覆盖）
 * @details 验证移除任务后位图被正确清除
 */
TEST_F(MrtkScheduleTest, RemoveTask_SingleTask_BitmapCleared) {
    /* Given: 单个就绪任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 5
#else
    #define TEST_PRIO 18
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* When: 移除任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* Then: 验证队列为空 */
    EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[TEST_PRIO]));

    /* Then: 验证节点指向自己 */
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.prev, &test_tcbs[0].sched_node);
#undef TEST_PRIO
}

/**
 * @test RemoveTask_OneOfMultiple_BitmapKept
 * @brief 测试移除多个任务中的一个（分支覆盖）
 * @details 验证移除一个任务后，如果队列还有任务，位图保持设置
 */
TEST_F(MrtkScheduleTest, RemoveTask_OneOfMultiple_BitmapKept) {
    /* Given: 多个同优先级任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 12
#else
    #define TEST_PRIO 16
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    InitTCB(&test_tcbs[1], TEST_PRIO);
    InitTCB(&test_tcbs[2], TEST_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* When: 移除中间的任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[1]);

    /* Then: 验证位图仍然被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* Then: 验证队列还有两个任务 */
    EXPECT_EQ(CountTasksInReadyQueue(TEST_PRIO), 2U);

    /* Then: 验证队列顺序正确 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[2].sched_node);
    EXPECT_EQ(test_tcbs[2].sched_node.next, &g_ready_task_list[TEST_PRIO]);
#undef TEST_PRIO
}

/**
 * @test RemoveTask_LastOfPriority_BitmapCleared
 * @brief 测试移除某优先级的最后一个任务（分支覆盖）
 * @details 验证移除某优先级的最后一个任务后，位图被清除
 */
TEST_F(MrtkScheduleTest, RemoveTask_LastOfPriority_BitmapCleared) {
    /* Given: 多个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK1_PRIO 5
    #define TASK2_PRIO 15
#else
    #define TASK1_PRIO 25
    #define TASK2_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TASK1_PRIO);
    InitTCB(&test_tcbs[1], TASK2_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);

    /* When: 移除 TASK1_PRIO 的任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证 TASK1_PRIO 的位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK1_PRIO));

    /* Then: 验证 TASK2_PRIO 的位图仍然被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK2_PRIO));
#undef TASK1_PRIO
#undef TASK2_PRIO
}

/**
 * @test RemoveTask_ThenInsert_BitmapSetAgain
 * @brief 测试移除后重新插入（状态机覆盖）
 * @details 验证任务的生命周期：插入 -> 移除 -> 插入
 */
TEST_F(MrtkScheduleTest, RemoveTask_ThenInsert_BitmapSetAgain) {
    /* Given: 已插入的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 9
#else
    #define TEST_PRIO 19
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* When: 移除任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));

    /* When: 重新插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* Then: 验证位图被重新设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TEST_PRIO));
#undef TEST_PRIO
}

/* =============================================================================
 * mrtk_schedule 测试
 * ============================================================================= */

/**
 * @test Schedule_OnlySystemTasks_SelectTimerDaemon
 * @brief 测试只有系统任务的调度（边界值分析）
 * @details 验证当只有系统任务（空闲任务 + 定时器守护任务）就绪时，
 *          调度器选择定时器守护任务（优先级高于空闲任务）
 * @note 系统初始化后默认就绪队列中有两个系统任务：
 *       - 空闲任务（优先级 MRTK_IDLE_PRIORITY，最低优先级）
 *       - 定时器守护任务（优先级 MRTK_TIMER_TASK_PRIO = 4，高于空闲任务）
 */
TEST_F(MrtkScheduleTest, Schedule_OnlySystemTasks_SelectTimerDaemon) {
    /* Given: 系统已初始化（只有系统任务：空闲任务 + 定时器守护任务） */

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该指向定时器守护任务（优先级高于空闲任务） */
#if (MRTK_USING_TIMER_SOFT == 1)
    mrtk_task_t *timer_daemon = mrtk_task_get_timer_daemon();
    EXPECT_EQ(g_NextTCB, timer_daemon);
#else
    /* 如果未启用软定时器，则只有空闲任务 */
    mrtk_task_t *idle_task = mrtk_task_get_idle();
    EXPECT_EQ(g_NextTCB, idle_task);
#endif
}

/**
 * @test Schedule_SingleTask_SelectTask
 * @brief 测试单个用户任务的调度（等价类划分）
 * @details 验证当只有一个用户任务就绪时（系统任务除外），调度器选择该用户任务
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能被调度器选择
 */
TEST_F(MrtkScheduleTest, Schedule_SingleTask_SelectTask) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 添加单个用户任务（优先级高于定时器守护任务） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要 < MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 3
#else
    /* 优先级值越大优先级越高，需要 > MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该指向该用户任务（优先级高于系统任务） */
    EXPECT_EQ(g_NextTCB, &test_tcbs[0]);
#undef TEST_PRIO
}

/**
 * @test Schedule_HighestPriorityTask_SelectHighest
 * @brief 测试最高优先级任务调度（等价类划分）
 * @details 验证调度器选择最高优先级的任务（系统任务 + 用户任务）
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能成为最高优先级任务
 */
TEST_F(MrtkScheduleTest, Schedule_HighestPriorityTask_SelectHighest) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 添加多个不同优先级的用户任务（都高于定时器守护任务） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要都 < MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 1
    #define TASK2_PRIO 2
    #define TASK3_PRIO 3
#else
    /* 优先级值越大优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 25
    #define TASK2_PRIO 15
    #define TASK3_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TASK1_PRIO);
    InitTCB(&test_tcbs[1], TASK2_PRIO);
    InitTCB(&test_tcbs[2], TASK3_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该指向最高优先级任务（TASK1） */
    EXPECT_EQ(g_NextTCB, &test_tcbs[0]);
#undef TASK1_PRIO
#undef TASK2_PRIO
#undef TASK3_PRIO
}

/**
 * @test Schedule_CurrentIsHighest_NoSwitch
 * @brief 测试当前任务就是最高优先级任务（分支覆盖）
 * @details 验证当最高优先级任务就是当前任务时，不触发上下文切换
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       当前用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能成为最高优先级任务
 */
TEST_F(MrtkScheduleTest, Schedule_CurrentIsHighest_NoSwitch) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 当前任务和最高优先级任务相同 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要 < MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 3
#else
    /* 优先级值越大优先级越高，需要 > MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    g_CurrentTCB = &test_tcbs[0];

    /* 期望：不调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该等于 g_CurrentTCB */
    EXPECT_EQ(g_NextTCB, g_CurrentTCB);
#undef TEST_PRIO
}

/**
 * @test Schedule_HighestNotCurrent_TriggerSwitch
 * @brief 测试最高优先级任务不是当前任务（分支覆盖）
 * @details 验证当最高优先级任务不是当前任务时，触发上下文切换
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能触发用户任务之间的上下文切换
 */
TEST_F(MrtkScheduleTest, Schedule_HighestNotCurrent_TriggerSwitch) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 当前任务和最高优先级任务不同 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要都 < MRTK_TIMER_TASK_PRIO (4) */
    #define CURRENT_PRIO 2
    #define HIGHEST_PRIO 1
#else
    /* 优先级值越大优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 20
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该指向最高优先级任务 */
    EXPECT_EQ(g_NextTCB, &test_tcbs[0]);
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/**
 * @test Schedule_SamePriorityDifferentTask_SwitchToNext
 * @brief 测试同优先级不同任务（等价类划分）
 * @details 验证同优先级任务按 FIFO 顺序调度
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能测试用户任务之间的调度
 */
TEST_F(MrtkScheduleTest, Schedule_SamePriorityDifferentTask_SwitchToNext) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 两个同优先级用户任务，当前任务为第一个 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要 < MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 3
#else
    /* 优先级值越大优先级越高，需要 > MRTK_TIMER_TASK_PRIO (4) */
    #define TEST_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    InitTCB(&test_tcbs[1], TEST_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[0];

    /* 期望：触发上下文切换（虽然优先级相同，但队列中下一个任务不同） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);

    /* When: 调用 mrtk_task_yield() */
    mrtk_task_yield();

    /* Then: g_NextTCB 应该指向队列的下一个任务（test_tcbs[1]） */
    EXPECT_EQ(g_NextTCB, &test_tcbs[1]);
#undef TEST_PRIO
}

/* =============================================================================
 * mrtk_schedule_lock / unlock 测试
 * ============================================================================= */

/**
 * @test ScheduleLockUnlock_SingleLockUnlock_Success
 * @brief 测试单次锁定和解锁（状态机覆盖）
 * @details 验证调度锁的基本功能
 */
TEST_F(MrtkScheduleTest, ScheduleLockUnlock_SingleLockUnlock_Success) {
    /* Given: 初始化状态 */

    /* When: 锁定调度器 */
    mrtk_schedule_lock();

    /* Then: 验证调度锁嵌套计数器为 1 */
    EXPECT_EQ(g_schedule_lock_nest, 1U);

    /* When: 解锁调度器 */
    mrtk_schedule_unlock();

    /* Then: 验证调度锁嵌套计数器为 0 */
    EXPECT_EQ(g_schedule_lock_nest, 0U);
}

/**
 * @test ScheduleLockUnlock_MultipleLockUnlock_NestingCountCorrect
 * @brief 测试多次锁定和解锁（边界值分析）
 * @details 验证调度锁的嵌套功能
 */
TEST_F(MrtkScheduleTest, ScheduleLockUnlock_MultipleLockUnlock_NestingCountCorrect) {
    /* Given: 初始化状态 */

    /* When: 多次锁定调度器 */
    mrtk_schedule_lock();
    mrtk_schedule_lock();
    mrtk_schedule_lock();

    /* Then: 验证调度锁嵌套计数器为 3 */
    EXPECT_EQ(g_schedule_lock_nest, 3U);

    /* When: 解锁一次 */
    mrtk_schedule_unlock();

    /* Then: 验证调度锁嵌套计数器为 2 */
    EXPECT_EQ(g_schedule_lock_nest, 2U);

    /* When: 解锁两次 */
    mrtk_schedule_unlock();
    mrtk_schedule_unlock();

    /* Then: 验证调度锁嵌套计数器为 0 */
    EXPECT_EQ(g_schedule_lock_nest, 0U);
}

/**
 * @test Schedule_Locked_ScheduleDelayed
 * @brief 测试调度器锁定时调度被延迟（分支覆盖）
 * @details 验证锁定期间调用调度，调度被延迟到解锁时
 */
TEST_F(MrtkScheduleTest, Schedule_Locked_ScheduleDelayed) {
    /* Given: 锁定的调度器 */
    mrtk_schedule_lock();
    EXPECT_EQ(g_schedule_lock_nest, 1U);

    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 5
#else
    #define CURRENT_PRIO 5
    #define HIGHEST_PRIO 25
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：不调用上下文切换（因为调度器被锁定） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: 验证设置了延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);

    /* Then: 验证 g_NextTCB 未被设置 */
    EXPECT_EQ(g_NextTCB, nullptr);

    /* When: 解锁调度器 */
    testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);
    mrtk_schedule_unlock();

    /* Then: 验证调度锁嵌套计数器为 0 */
    EXPECT_EQ(g_schedule_lock_nest, 0U);
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/**
 * @test Schedule_NestedLock_ScheduleDelayedUntilFullUnlock
 * @brief 测试嵌套锁定时调度被延迟（分支覆盖）
 * @details 验证嵌套锁定期间调用调度，调度被延迟到完全解锁时
 */
TEST_F(MrtkScheduleTest, Schedule_NestedLock_ScheduleDelayedUntilFullUnlock) {
    /* Given: 嵌套锁定的调度器 */
    mrtk_schedule_lock();
    mrtk_schedule_lock();
    EXPECT_EQ(g_schedule_lock_nest, 2U);

    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 5
#else
    #define CURRENT_PRIO 5
    #define HIGHEST_PRIO 25
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：不调用上下文切换（因为调度器被锁定） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: 验证设置了延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);

    /* When: 解锁一次（嵌套计数器仍大于 0） */
    mrtk_schedule_unlock();

    /* Then: 验证嵌套计数器为 1，调度仍未触发 */
    EXPECT_EQ(g_schedule_lock_nest, 1U);

    /* When: 完全解锁 */
    testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);
    mrtk_schedule_unlock();

    /* Then: 验证嵌套计数器为 0 */
    EXPECT_EQ(g_schedule_lock_nest, 0U);
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/* =============================================================================
 * 中断上下文测试
 * ============================================================================= */

/**
 * @test Schedule_InInterrupt_ScheduleDelayed
 * @brief 测试中断上下文中调度被延迟（分支覆盖）
 * @details 验证在中断中调用调度，调度被延迟到退出中断时
 */
TEST_F(MrtkScheduleTest, Schedule_InInterrupt_ScheduleDelayed) {
    /* Given: 中断上下文（模拟中断嵌套） */
    g_interrupt_nest = 1;

    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 5
#else
    #define CURRENT_PRIO 5
    #define HIGHEST_PRIO 25
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：不调用上下文切换（因为在中断中） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: 验证设置了延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);

    /* Then: 验证 g_NextTCB 未被设置 */
    EXPECT_EQ(g_NextTCB, nullptr);

    /* 清理 */
    g_interrupt_nest = 0;
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/**
 * @test Schedule_InInterruptWithLock_ScheduleDelayed
 * @brief 测试中断上下文且调度器锁定时调度被延迟（分支覆盖）
 * @details 验证中断和锁定同时存在时的行为
 */
TEST_F(MrtkScheduleTest, Schedule_InInterruptWithLock_ScheduleDelayed) {
    /* Given: 中断上下文且调度器锁定 */
    g_interrupt_nest = 1;
    mrtk_schedule_lock();

    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 5
#else
    #define CURRENT_PRIO 5
    #define HIGHEST_PRIO 25
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：不调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: 验证设置了延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);

    /* 清理 */
    mrtk_schedule_unlock();
    g_interrupt_nest = 0;
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/**
 * @test Schedule_NestedInterrupt_ScheduleDelayed
 * @brief 测试嵌套中断时调度被延迟（边界值分析）
 * @details 验证多层中断嵌套时的行为
 */
TEST_F(MrtkScheduleTest, Schedule_NestedInterrupt_ScheduleDelayed) {
    /* Given: 嵌套中断上下文 */
    g_interrupt_nest = 3;

    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define CURRENT_PRIO 10
    #define HIGHEST_PRIO 5
#else
    #define CURRENT_PRIO 5
    #define HIGHEST_PRIO 25
#endif
    InitTCB(&test_tcbs[0], HIGHEST_PRIO);
    InitTCB(&test_tcbs[1], CURRENT_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];

    /* 期望：不调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: 验证设置了延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);

    /* 清理 */
    g_interrupt_nest = 0;
#undef CURRENT_PRIO
#undef HIGHEST_PRIO
}

/* =============================================================================
 * _mrtk_schedule_prio_change 测试
 * ============================================================================= */

/**
 * @test PrioChange_ValidChange_Success
 * @brief 测试任务优先级修改（状态机覆盖）
 * @details 验证任务优先级修改后，位图和队列都正确更新
 */
TEST_F(MrtkScheduleTest, PrioChange_ValidChange_Success) {
    /* Given: 已插入的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define OLD_PRIO 15
    #define NEW_PRIO 5
#else
    #define OLD_PRIO 5
    #define NEW_PRIO 25
#endif
    InitTCB(&test_tcbs[0], OLD_PRIO);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);

    /* When: 修改优先级 */
    _mrtk_schedule_prio_change((mrtk_task_t *) &test_tcbs[0], NEW_PRIO);

    /* Then: 验证任务优先级被修改 */
    EXPECT_EQ(test_tcbs[0].priority, NEW_PRIO);

    /* Then: 验证 OLD_PRIO 的位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, OLD_PRIO));

    /* Then: 验证 NEW_PRIO 的位图被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, NEW_PRIO));

    /* Then: 验证任务在 NEW_PRIO 队列中 */
    EXPECT_EQ(g_ready_task_list[NEW_PRIO].next, &test_tcbs[0].sched_node);
#undef OLD_PRIO
#undef NEW_PRIO
}

/**
 * @test PrioChange_MultipleTasks_CorrectOrder
 * @brief 测试多个任务的优先级修改（等价类划分）
 * @details 验证多个任务修改优先级后，调度顺序正确
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能测试用户任务之间的优先级变更
 */
TEST_F(MrtkScheduleTest, PrioChange_MultipleTasks_CorrectOrder) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 多个不同优先级的用户任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 15
    #define TASK2_PRIO 20
    #define TASK1_NEW_PRIO 5
#else
    /* 优先级值越大优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 10
    #define TASK2_PRIO 5
    #define TASK1_NEW_PRIO 28
#endif
    InitTCB(&test_tcbs[0], TASK1_PRIO);
    InitTCB(&test_tcbs[1], TASK2_PRIO);

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);

    /* When: 将 test_tcbs[0] 的优先级提升到最高 */
    _mrtk_schedule_prio_change((mrtk_task_t *) &test_tcbs[0], TASK1_NEW_PRIO);

    /* When: 调用调度 */
    mrtk_schedule();

    /* Then: g_NextTCB 应该指向 test_tcbs[0]（最高优先级） */
    EXPECT_EQ(g_NextTCB, &test_tcbs[0]);
#undef TASK1_PRIO
#undef TASK2_PRIO
#undef TASK1_NEW_PRIO
}

/* =============================================================================
 * mrtk_schedule_prio_ht / mrtk_schedule_prio_lt 测试
 * ============================================================================= */

/**
 * @test PrioHt_LhsHigher_ReturnTrue
 * @brief 测试优先级比较函数（分支覆盖）
 * @details 验证 mrtk_schedule_prio_ht 正确判断优先级高低
 */
TEST_F(MrtkScheduleTest, PrioHt_LhsHigher_ReturnTrue) {
    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK_HIGH_PRIO 5
    #define TASK_LOW_PRIO  10
#else
    #define TASK_HIGH_PRIO 25
    #define TASK_LOW_PRIO  10
#endif
    mrtk_task_t task1, task2;
    memset(&task1, 0, sizeof(task1));
    memset(&task2, 0, sizeof(task2));
    task1.priority = TASK_HIGH_PRIO;
    task2.priority = TASK_LOW_PRIO;

    /* When & Then: task1 优先级高于 task2 */
    EXPECT_TRUE(mrtk_schedule_prio_ht(&task1, &task2));
    EXPECT_FALSE(mrtk_schedule_prio_ht(&task2, &task1));
#undef TASK_HIGH_PRIO
#undef TASK_LOW_PRIO
}

/**
 * @test PrioLt_LhsLower_ReturnTrue
 * @brief 测试优先级比较函数（分支覆盖）
 * @details 验证 mrtk_schedule_prio_lt 正确判断优先级低高
 */
TEST_F(MrtkScheduleTest, PrioLt_LhsLower_ReturnTrue) {
    /* Given: 两个不同优先级的任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TASK_HIGH_PRIO 5
    #define TASK_LOW_PRIO  10
#else
    #define TASK_HIGH_PRIO 25
    #define TASK_LOW_PRIO  10
#endif
    mrtk_task_t task1, task2;
    memset(&task1, 0, sizeof(task1));
    memset(&task2, 0, sizeof(task2));
    task1.priority = TASK_LOW_PRIO;
    task2.priority = TASK_HIGH_PRIO;

    /* When & Then: task1 优先级低于 task2 */
    EXPECT_TRUE(mrtk_schedule_prio_lt(&task1, &task2));
    EXPECT_FALSE(mrtk_schedule_prio_lt(&task2, &task1));
#undef TASK_HIGH_PRIO
#undef TASK_LOW_PRIO
}

/**
 * @test PrioHt_EqualPriority_ReturnFalse
 * @brief 测试相等优先级的比较（边界值分析）
 * @details 验证优先级相等时，比较函数返回 false
 */
TEST_F(MrtkScheduleTest, PrioHt_EqualPriority_ReturnFalse) {
    /* Given: 两个相同优先级的任务 */
    mrtk_task_t task1, task2;
    memset(&task1, 0, sizeof(task1));
    memset(&task2, 0, sizeof(task2));
    task1.priority = 10;
    task2.priority = 10;

    /* When & Then: 优先级相等，都返回 false */
    EXPECT_FALSE(mrtk_schedule_prio_ht(&task1, &task2));
    EXPECT_FALSE(mrtk_schedule_prio_lt(&task1, &task2));
}

/* =============================================================================
 * 综合场景测试
 * ============================================================================= */

/**
 * @test FullWorkflow_InsertScheduleRemove_Lifecycle
 * @brief 测试完整的调度器工作流（状态机覆盖）
 * @details 模拟真实的任务调度场景：插入 -> 调度 -> 移除
 * @note 系统初始化后已有系统任务（空闲任务 + 定时器守护任务）
 *       用户任务的优先级需要高于定时器守护任务（MRTK_TIMER_TASK_PRIO = 4）
 *       才能测试用户任务的完整调度生命周期
 */
TEST_F(MrtkScheduleTest, FullWorkflow_InsertScheduleRemove_Lifecycle) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 多个不同优先级的用户任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 优先级值越小优先级越高，需要都 < MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 1
    #define TASK2_PRIO 2
    #define TASK3_PRIO 2
#else
    /* 优先级值越大优先级越高，需要都 > MRTK_TIMER_TASK_PRIO (4) */
    #define TASK1_PRIO 25
    #define TASK2_PRIO 10
    #define TASK3_PRIO 10
#endif
    InitTCB(&test_tcbs[0], TASK1_PRIO);
    InitTCB(&test_tcbs[1], TASK2_PRIO);
    InitTCB(&test_tcbs[2], TASK3_PRIO);

    /* Step 1: 插入任务 */
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);

    /* 验证：所有优先级位图都被设置 */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK1_PRIO));
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK2_PRIO));

    /* Step 2: 第一次调度（选择最高优先级任务） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);
    mrtk_schedule();
    EXPECT_EQ(g_NextTCB, &test_tcbs[0]);
    g_CurrentTCB = &test_tcbs[0];
    testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);

    /* Step 3: 移除最高优先级任务 */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[0]);

    /* 验证：TASK1_PRIO 的位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK1_PRIO));

    /* Step 4: 第二次调度（选择 TASK2_PRIO 的第一个任务） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);
    mrtk_schedule();
    EXPECT_EQ(g_NextTCB, &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[1];
    testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);

    /* Step 5: 移除 test_tcbs[1] */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[1]);

    /* 验证：TASK2_PRIO 的位图仍然被设置（还有 test_tcbs[2]） */
    EXPECT_TRUE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK2_PRIO));

    /* Step 6: 移除 test_tcbs[2] */
    _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[2]);

    /* 验证：TASK2_PRIO 的位图被清除 */
    EXPECT_FALSE(MRTK_GET_BIT_VAL(g_ready_prio_bitmap, TASK2_PRIO));
#undef TASK1_PRIO
#undef TASK2_PRIO
#undef TASK3_PRIO
}

/**
 * @test StressTest_MultipleInsertRemoveSchedule_Stable
 * @brief 测试压力场景（边界值分析）
 * @details 验证大量插入、移除、调度操作后，系统状态依然正确
 * @note 最终就绪队列中应该只有系统任务（空闲任务 + 定时器守护任务）
 */
TEST_F(MrtkScheduleTest, StressTest_MultipleInsertRemoveSchedule_Stable) {
    /* Given: 系统已初始化（有系统任务） */

    /* Given: 多个用户任务 */
    for (mrtk_u32_t i = 0; i < 10; i++) {
        InitTCB(&test_tcbs[i], (mrtk_u8_t) (i % MRTK_MAX_PRIO_LEVEL_NUM));
    }

    /* When: 多次插入、移除、调度 */
    for (mrtk_u32_t round = 0; round < 3; round++) {
        /* 插入所有任务 */
        for (mrtk_u32_t i = 0; i < 10; i++) {
            _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[i]);
        }

        /* 验证位图一致性 */
        EXPECT_TRUE(VerifyBitmapConsistency());

        /* 调度 */
        mrtk_schedule();

        /* 移除所有任务 */
        for (mrtk_u32_t i = 0; i < 10; i++) {
            _mrtk_schedule_remove_task((mrtk_task_t *) &test_tcbs[i]);
        }

        /* 验证位图一致性 */
        EXPECT_TRUE(VerifyBitmapConsistency());
    }

    /* Then: 验证最终状态就绪队列中只有系统任务 */
    /* 验证空闲任务优先级的队列不为空 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[MRTK_IDLE_PRIORITY]));

#if (MRTK_USING_TIMER_SOFT == 1)
    /* 验证定时器守护任务优先级的队列不为空 */
    EXPECT_FALSE(_mrtk_list_is_empty(&g_ready_task_list[MRTK_TIMER_TASK_PRIO]));
#endif

    /* 验证其他优先级队列为空 */
    for (mrtk_u32_t i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
        if (i != MRTK_IDLE_PRIORITY) {
#if (MRTK_USING_TIMER_SOFT == 1)
            if (i != MRTK_TIMER_TASK_PRIO) {
#endif
                EXPECT_TRUE(_mrtk_list_is_empty(&g_ready_task_list[i]));
#if (MRTK_USING_TIMER_SOFT == 1)
            }
#endif
        }
    }

    /* 验证位图中只有系统任务优先级的位被设置 */
    mrtk_u32_t expected_bitmap = (1U << MRTK_IDLE_PRIORITY);
#if (MRTK_USING_TIMER_SOFT == 1)
    expected_bitmap |= (1U << MRTK_TIMER_TASK_PRIO);
#endif
    EXPECT_EQ(g_ready_prio_bitmap, expected_bitmap);
}

/* =============================================================================
 * mrtk_tick_increase 测试
 * ============================================================================= */

/**
 * @test TickIncrease_SingleCall_TickIncremented
 * @brief 测试单次调用 Tick 递增（状态机覆盖）
 * @details 验证调用 mrtk_tick_increase() 后全局 Tick 计数器递增 1
 */
TEST_F(MrtkScheduleTest, TickIncrease_SingleCall_TickIncremented) {
    /* Given: 系统已初始化 */

    /* Given: 获取初始 Tick 值 */
    mrtk_u32_t initial_tick = mrtk_mock_get_tick();

    /* When: 调用 mrtk_tick_increase() */
    mrtk_tick_increase();

    /* Then: 验证 Tick 值递增 1 */
    EXPECT_EQ(mrtk_mock_get_tick(), initial_tick + 1);
}

/**
 * @test TickIncrease_MultipleCalls_TickAccumulated
 * @brief 测试多次调用 Tick 累加（边界值分析）
 * @details 验证多次调用 mrtk_tick_increase() 后 Tick 值正确累加
 */
TEST_F(MrtkScheduleTest, TickIncrease_MultipleCalls_TickAccumulated) {
    /* Given: 系统已初始化 */

    /* Given: 获取初始 Tick 值 */
    mrtk_u32_t initial_tick = mrtk_mock_get_tick();

    /* When: 多次调用 mrtk_tick_increase() */
    for (mrtk_u32_t i = 0; i < 100; i++) {
        mrtk_tick_increase();
    }

    /* Then: 验证 Tick 值递增 100 */
    EXPECT_EQ(mrtk_mock_get_tick(), initial_tick + 100);
}

/**
 * @test TickIncrease_TimeSliceExhausted_TaskMovedToBack
 * @brief 测试时间片用尽时任务移到队列末尾（状态机覆盖）
 * @details 验证当任务时间片用尽时，任务被移到同优先级队列末尾
 * @note 时间片用尽条件：remain_tick 递减到 0
 */
TEST_F(MrtkScheduleTest, TickIncrease_TimeSliceExhausted_TaskMovedToBack) {
    /* Given: 系统已初始化 */

    /* Given: 两个同优先级用户任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 5
#else
    #define TEST_PRIO 20
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    InitTCB(&test_tcbs[1], TEST_PRIO);

    /* 设置时间片为 2 */
    test_tcbs[0].init_tick = 2;
    test_tcbs[0].remain_tick = 2;
    test_tcbs[1].init_tick = 2;
    test_tcbs[1].remain_tick = 2;

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    g_CurrentTCB = &test_tcbs[0];

    /* 验证初始队列顺序：test_tcbs[0] -> test_tcbs[1] */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[1].sched_node);

    /* When: 时间片用尽（调用两次 mrtk_tick_increase） */
    mrtk_tick_increase();
    EXPECT_EQ(test_tcbs[0].remain_tick, 1U);

    mrtk_tick_increase();

    /* Then: 验证任务被移到队列末尾 */
    EXPECT_EQ(test_tcbs[0].remain_tick, 2U);  /* 时间片被重置 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[1].sched_node);
    EXPECT_EQ(test_tcbs[1].sched_node.next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &g_ready_task_list[TEST_PRIO]);

    /* Then: 验证延迟调度标志被设置 */
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);
#undef TEST_PRIO
}

/**
 * @test TickIncrease_MultipleTasksSamePriority_RoundRobin
 * @brief 测试同优先级多任务的轮转调度（等价类划分）
 * @details 验证同优先级任务按时间片轮转调度
 * @note 模拟 3 个任务的轮转调度场景
 */
TEST_F(MrtkScheduleTest, TickIncrease_MultipleTasksSamePriority_RoundRobin) {
    /* Given: 系统已初始化 */

    /* Given: 三个同优先级用户任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 6
#else
    #define TEST_PRIO 18
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);
    InitTCB(&test_tcbs[1], TEST_PRIO);
    InitTCB(&test_tcbs[2], TEST_PRIO);

    /* 设置时间片为 1 */
    for (mrtk_u32_t i = 0; i < 3; i++) {
        test_tcbs[i].init_tick = 1;
        test_tcbs[i].remain_tick = 1;
    }

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[1]);
    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[2]);
    g_CurrentTCB = &test_tcbs[0];

    /* 验证初始队列顺序：0 -> 1 -> 2 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[1].sched_node);
    EXPECT_EQ(test_tcbs[1].sched_node.next, &test_tcbs[2].sched_node);

    /* When: test_tcbs[0] 时间片用尽 */
    mrtk_tick_increase();

    /* Then: 验证队列顺序变为：1 -> 2 -> 0 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[1].sched_node);
    EXPECT_EQ(test_tcbs[1].sched_node.next, &test_tcbs[2].sched_node);
    EXPECT_EQ(test_tcbs[2].sched_node.next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &g_ready_task_list[TEST_PRIO]);

    /* 清除延迟调度标志 */
    g_need_schedule = MRTK_FALSE;
    g_CurrentTCB = &test_tcbs[1];

    /* When: test_tcbs[1] 时间片用尽 */
    mrtk_tick_increase();

    /* Then: 验证队列顺序变为：2 -> 0 -> 1 */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[2].sched_node);
    EXPECT_EQ(test_tcbs[2].sched_node.next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &test_tcbs[1].sched_node);
    EXPECT_EQ(test_tcbs[1].sched_node.next, &g_ready_task_list[TEST_PRIO]);
#undef TEST_PRIO
}

/**
 * @test TickIncrease_SingleTaskNoRotation_NoSchedule
 * @brief 测试单个任务不触发轮转调度（分支覆盖）
 * @details 验证当同优先级队列中只有一个任务时，不触发轮转调度
 * @note 即使时间片用尽，也不应触发调度（因为队列中只有一个任务）
 */
TEST_F(MrtkScheduleTest, TickIncrease_SingleTaskNoRotation_NoSchedule) {
    /* Given: 系统已初始化 */

    /* Given: 单个用户任务 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    #define TEST_PRIO 7
#else
    #define TEST_PRIO 22
#endif
    InitTCB(&test_tcbs[0], TEST_PRIO);

    /* 设置时间片为 1 */
    test_tcbs[0].init_tick = 1;
    test_tcbs[0].remain_tick = 1;

    _mrtk_schedule_insert_task((mrtk_task_t *) &test_tcbs[0]);
    g_CurrentTCB = &test_tcbs[0];

    /* When: 时间片用尽 */
    mrtk_tick_increase();

    /* Then: 验证时间片被重置 */
    EXPECT_EQ(test_tcbs[0].remain_tick, 1U);

    /* Then: 验证任务仍在队列中（未被移除再插入） */
    EXPECT_EQ(g_ready_task_list[TEST_PRIO].next, &test_tcbs[0].sched_node);
    EXPECT_EQ(test_tcbs[0].sched_node.next, &g_ready_task_list[TEST_PRIO]);

    /* Then: 验证未设置延迟调度标志 */
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
#undef TEST_PRIO
}

/**
 * @test TickIncrease_TickOverflow_WrapAround
 * @brief 测试 32 位 Tick 回绕场景（边界值分析）
 * @details 验证 Tick 计数器在接近最大值时能正确回绕
 * @note 利用无符号整数的自然溢出特性，无需特殊处理
 */
TEST_F(MrtkScheduleTest, TickIncrease_TickOverflow_WrapAround) {
    /* Given: 系统已初始化 */

    /* Given: 设置 Tick 值接近最大值 */
    mrtk_mock_set_tick(0xFFFFFFFE);

    /* When: Tick 溢出 */
    mrtk_tick_increase();

    /* Then: 验证 Tick 值回绕到 0 */
    EXPECT_EQ(mrtk_mock_get_tick(), 0xFFFFFFFFU);

    /* When: 再次递增 */
    mrtk_tick_increase();

    /* Then: 验证 Tick 值回绕到 0 */
    EXPECT_EQ(mrtk_mock_get_tick(), 0U);
}