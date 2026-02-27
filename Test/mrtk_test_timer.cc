/**
 * @file mrtk_timer_test.cpp
 * @author leiyx
 * @brief 定时器管理模块单元测试
 * @details 测试定时器的初始化、启动、停止、双链表隔离、32位回卷、周期定时器无漂移等功能
 *
 * 测试覆盖策略：
 * 1. 边界值分析：timeout=0、MRTK_WAITING_FOREVER、32位回卷边界、最大/最小定时时间
 * 2. 等价类划分：NULL指针防御、非法参数、合法参数、满/空链表状态
 * 3. 分支覆盖：所有 if/else 分支（启动/停止、插入/移除、单次/周期、硬/软模式）
 * 4. 状态机覆盖：Init -> Start -> Running -> Stop/Timeout -> Delete（完整闭环）
 * 5. 32位回卷场景：tick 接近 UINT32_MAX 时定时器正常工作
 * 6. 周期定时器零漂移：验证累加算法 `timeout_tick += init_tick`
 * 7. 双链表隔离：验证硬定时器和软定时器完全隔离
 * 8. 临界区最小化：验证硬定时器回调前主动开中断
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/* Mock 头文件（C++ 代码，必须在最前） */
#include "mrtk_mock_hw.hpp"

#if (MRTK_USING_TIMER == 1)

/* MRTK 头文件（已包含 extern "C"） */
#include "mrtk.h"

/* =============================================================================
 * 测试夹具：TimerTest
 * ============================================================================== */

/**
 * @class MrtkTimerTest
 * @brief 定时器测试的测试夹具
 * @details 提供时间控制引擎（AdvanceTick/SetTick）、Mock 对象管理和辅助方法
 */
class MrtkTimerTest : public ::testing::Test {
  protected:
    /**
     * @brief CPU 端口层 Mock 对象（使用 NiceMock 自动忽略未设置期望的调用，避免 GMOCK WARNING）
     */
    testing::NiceMock<MockCpuPort> mock_cpu_port;

    /**
     * @brief 定时器控制块数组（用于测试）
     */
    mrtk_timer_t test_timers[10];

    /**
     * @brief 回调函数调用计数器（用于验证触发次数）
     */
    static int callback_count;

    /**
     * @brief 最后一次回调的定时器指针（用于验证触发对象）
     */
    static mrtk_timer_t *last_callback_timer;

    /**
     * @brief 测试回调函数
     * @details 记录调用次数和触发定时器，用于验证定时器行为
     * @param[in] para 触发回调的定时器指针（通过 para 参数传递）
     */
    static void TestCallback(mrtk_void_t *para)
    {
        callback_count++;
        last_callback_timer = static_cast<mrtk_timer_t *>(para);
    }

    /**
     * @brief 测试前置设置
     * @details 初始化系统、Mock 对象
     */
    void SetUp() override
    {
        /* Step 1: 系统初始化（统一入口） */
        mrtk_err_t ret = mrtk_system_init();
        ASSERT_EQ(ret, MRTK_EOK) << "系统初始化失败";

        g_CurrentTCB = mrtk_task_get_idle();

        /* Step 2: 设置 Mock 对象 */
        mrtk_mock_set_cpu_port(&mock_cpu_port);

        /* Step 3: 设置默认期望行为 */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_disable).WillByDefault(testing::Return(0));
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_enable).WillByDefault(testing::Return());

        /* Step 4: 重置回调计数器 */
        callback_count         = 0;
        last_callback_timer = nullptr;
    }

    /**
     * @brief 测试后置清理
     * @details 清除 Mock 对象，重置堆内存
     */
    void TearDown() override
    {
        /* 重置堆内存，确保下个用例有干净的堆环境 */
        /* 强制重新初始化堆（重建哨兵节点，清空空闲列表） */
    mrtk_heap_init(g_heap_buffer, g_heap_buffer + MRTK_HEAP_SIZE);

        /* 清除 Mock 对象 */
        mrtk_mock_clear_cpu_port();
    }

    /* ==============================================================================
     * 时间控制引擎（Time Control Engine）
     * ============================================================================== */

    /**
     * @brief 推进系统时钟
     * @details 模拟系统时钟推进，用于测试定时器触发逻辑
     * @param[in] ticks 推进的 tick 数量
     */
    void AdvanceTick(mrtk_u32_t ticks)
    {
        mrtk_mock_advance_tick(ticks);
    }

    /**
     * @brief 设置系统时钟
     * @details 直接设置系统时钟值，用于测试 32 位回卷场景
     * @param[in] tick 要设置的 tick 值
     */
    void SetTick(mrtk_u32_t tick)
    {
        mrtk_mock_set_tick(tick);
    }

    /**
     * @brief 获取当前系统时钟
     * @return mrtk_u32_t 当前系统时钟值
     */
    mrtk_u32_t GetTick(void)
    {
        return mrtk_mock_get_tick();
    }

    /* ==============================================================================
     * 辅助函数
     * ============================================================================== */

    /**
     * @brief 验证硬定时器链表顺序
     * @details 遍历硬定时器链表，验证按 timeout_tick 升序排列
     * @return true 链表有序，false 链表无序
     */
    bool VerifyHardTimerListOrder(void)
    {
        extern mrtk_list_t g_hard_timer_list;

        mrtk_timer_t *prev = nullptr;
        mrtk_timer_t *timer;

        MRTK_LIST_FOR_EACH(timer, &g_hard_timer_list, mrtk_timer_t, timer_node)
        {
            if (prev != nullptr && prev->timeout_tick > timer->timeout_tick) {
                return false;
            }
            prev = timer;
        }
        return true;
    }

    /**
     * @brief 验证软定时器链表顺序
     * @details 遍历软定时器链表，验证按 timeout_tick 升序排列
     * @return true 链表有序，false 链表无序
     */
    bool VerifySoftTimerListOrder(void)
    {
        extern mrtk_list_t g_soft_timer_list;

        mrtk_timer_t *prev = nullptr;
        mrtk_timer_t *timer;

        MRTK_LIST_FOR_EACH(timer, &g_soft_timer_list, mrtk_timer_t, timer_node)
        {
            if (prev != nullptr && prev->timeout_tick > timer->timeout_tick) {
                return false;
            }
            prev = timer;
        }
        return true;
    }

    /**
     * @brief 获取硬定时器链表长度
     * @return mrtk_size_t 链表中的定时器数量
     */
    mrtk_size_t GetHardTimerListLength(void)
    {
        extern mrtk_list_t g_hard_timer_list;
        mrtk_size_t count      = 0;
        mrtk_timer_t *timer;

        MRTK_LIST_FOR_EACH(timer, &g_hard_timer_list, mrtk_timer_t, timer_node)
        {
            count++;
        }
        return count;
    }

    /**
     * @brief 获取软定时器链表长度
     * @return mrtk_size_t 链表中的定时器数量
     */
    mrtk_size_t GetSoftTimerListLength(void)
    {
        extern mrtk_list_t g_soft_timer_list;
        mrtk_size_t count      = 0;
        mrtk_timer_t *timer;

        MRTK_LIST_FOR_EACH(timer, &g_soft_timer_list, mrtk_timer_t, timer_node)
        {
            count++;
        }
        return count;
    }
};

/* 静态成员初始化 */
int MrtkTimerTest::callback_count         = 0;
mrtk_timer_t *MrtkTimerTest::last_callback_timer = nullptr;

/* ==============================================================================
 * 测试用例：防御性编程测试（NULL 指针防御）
 * ============================================================================== */

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_init
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_init
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerInit)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_init 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_init(nullptr, "null_timer", TestCallback, nullptr, 100, MRTK_TIMER_FLAG_HARD_TIMER);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_init（NULL 名称）
 * @brief Given 传入 MRTK_NULL 作为名称参数
 *        When 调用 mrtk_timer_init
 *        Then 应返回 MRTK_EINVAL
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：name == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerInit_NullName)
{
    // Step 1: Given - MRTK_NULL 名称指针

    // Step 2: When - 调用 mrtk_timer_init 传入 MRTK_NULL 名称
    mrtk_err_t ret = mrtk_timer_init(&test_timers[0], nullptr, TestCallback, nullptr, 100, MRTK_TIMER_FLAG_HARD_TIMER);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 名称应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_init（NULL 回调）
 * @brief Given 传入 MRTK_NULL 作为回调函数
 *        When 调用 mrtk_timer_init
 *        Then 应返回 MRTK_EINVAL
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：callback == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerInit_NullCallback)
{
    // Step 1: Given - MRTK_NULL 回调函数

    // Step 2: When - 调用 mrtk_timer_init 传入 MRTK_NULL 回调
    mrtk_err_t ret = mrtk_timer_init(&test_timers[0], "test_timer", nullptr, nullptr, 100, MRTK_TIMER_FLAG_HARD_TIMER);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 回调应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_start
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_start
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerStart)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_start 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_start(nullptr);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_stop
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_stop
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerStop)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_stop 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_stop(nullptr);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_control
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_control
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerControl)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_control 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_control(nullptr, MRTK_TIMER_CMD_GET_TIME, nullptr);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_detach
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_detach
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerDetach)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_detach 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_detach(nullptr);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/**
 * @test MRTK_NULL 指针防御测试：mrtk_timer_delete
 * @brief Given 传入 MRTK_NULL 指针作为定时器控制块
 *        When 调用 mrtk_timer_delete
 *        Then 应返回 MRTK_EINVAL，不发生崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：timer == MRTK_NULL 分支
 */
TEST_F(MrtkTimerTest, NullPointerDefense_TimerDelete)
{
    // Step 1: Given - MRTK_NULL 定时器指针

    // Step 2: When - 调用 mrtk_timer_delete 传入 MRTK_NULL
    mrtk_err_t ret = mrtk_timer_delete(nullptr);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "传入 MRTK_NULL 指针应返回 MRTK_EINVAL";
}

/* ==============================================================================
 * 测试用例：边界值测试
 * ============================================================================== */

/**
 * @test 边界值测试：MRTK_WAITING_FOREVER
 * @brief Given 初始化一个超时时间为 MRTK_WAITING_FOREVER 的定时器
 *        When 调用 mrtk_timer_start
 *        Then 应返回 MRTK_EINVAL
 * @note 边界值分析：MRTK_WAITING_FOREVER 边界
 * @note 分支覆盖：timer->init_tick == MRTK_WAITING_FOREVER 分支
 */
TEST_F(MrtkTimerTest, BoundaryValue_WaitingForever)
{
    // Step 1: Given - 初始化超时时间为 MRTK_WAITING_FOREVER 的定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "max_timeout_timer", TestCallback, &test_timers[0],
                    MRTK_WAITING_FOREVER, MRTK_TIMER_FLAG_HARD_TIMER);

    // Step 2: When - 尝试启动定时器
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);

    // Step 3: Then - 应返回 MRTK_EINVAL
    EXPECT_EQ(ret, MRTK_EINVAL) << "软件定时器不支持永久等待";
}

/**
 * @test 边界值测试：超时时间为 0
 * @brief Given 启动一个超时为 0 的定时器
 *        When 立即调用检查函数
 *        Then 定时器应立即触发
 * @note 边界值分析：0 边界
 */
TEST_F(MrtkTimerTest, BoundaryValue_ZeroTimeoutFiresImmediately)
{
    // Step 1: Given - 初始化并启动超时为 0 的定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "zero_timeout_timer", TestCallback, &test_timers[0], 0,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证 timeout_tick = 0
    EXPECT_EQ(test_timers[0].timeout_tick, 0) << "timeout_tick 应为 0";

    // Step 3: When - 立即调用检查函数
    mrtk_timer_hard_check();

    // Step 4: Then - 验证定时器触发
    EXPECT_EQ(callback_count, 1) << "超时为 0 的定时器应立即触发";
}

/* ==============================================================================
 * 测试用例：基础启动功能测试
 * ============================================================================== */

/**
 * @test 基础启动功能测试：硬定时器启动并触发
 * @brief Given 初始化并启动一个超时为 100 的硬定时器
 *        When 推进 tick 到 100
 *        Then 定时器应触发，回调函数被调用
 * @note 等价类划分：合法参数类
 * @note 状态机覆盖：Init -> Start -> Running -> Timeout
 */
TEST_F(MrtkTimerTest, BasicStartup_HardTimerFires)
{
    // Step 1: Given - 初始化硬定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "basic_hard_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK) << "硬定时器启动失败";

    // Step 2: When - 验证定时器在链表中
    EXPECT_EQ(GetHardTimerListLength(), 1) << "启动后定时器应在硬定时器链表中";
    EXPECT_EQ(test_timers[0].timeout_tick, 100) << "timeout_tick 应为 100";

    // Step 3: When - 推进到未超时点（tick = 99）
    SetTick(99);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 0) << "未到达超时点不应触发";

    // Step 4: When - 推进到超时点（tick = 100）
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 5: Then - 验证回调触发
    EXPECT_EQ(callback_count, 1) << "到达超时点应触发";
    EXPECT_EQ(last_callback_timer, &test_timers[0]) << "回调应为正确的定时器";

    // Step 6: Then - 验证单次定时器触发后从链表移除
    EXPECT_EQ(GetHardTimerListLength(), 0) << "单次定时器触发后应从链表移除";
}

/**
 * @test 基础启动功能测试：软定时器启动并触发
 * @brief Given 初始化并启动一个超时为 100 的软定时器
 *        When 推进 tick 到 100
 *        Then 定时器应触发，回调函数被调用
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, BasicStartup_SoftTimerFires)
{
    // Step 1: Given - 初始化软定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "basic_soft_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK) << "软定时器启动失败";

    // Step 2: When - 验证定时器在软定时器链表中
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "启动后定时器应在软定时器链表中";

    // Step 3: When - 推进到超时点
    SetTick(100);
    mrtk_timer_soft_check();

    // Step 4: Then - 验证回调触发
    EXPECT_EQ(callback_count, 1) << "软定时器应触发";
}

/**
 * @test 单次定时器测试：触发后不重启
 * @brief Given 启动一个单次定时器
 *        When 定时器触发后
 *        Then 定时器应从链表中移除，不再触发
 * @note 状态机覆盖：Running -> Timeout -> Deleted
 * @note 分支覆盖：!(first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC) 分支
 */
TEST_F(MrtkTimerTest, OneShotTimer_NotRestartAfterFired)
{
    // Step 1: Given - 初始化单次定时器（默认为单次模式）
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "oneshot_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 推进到第一次超时点
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第一次应触发";

    // Step 3: Then - 验证定时器从链表中移除
    EXPECT_EQ(GetHardTimerListLength(), 0) << "单次定时器触发后应从链表移除";
    EXPECT_FALSE(test_timers[0].obj.flag & MRTK_TIMER_FLAG_ACTIVATED) << "激活标志应被清除";

    // Step 4: When - 推进到下一个周期点（tick = 200）
    callback_count = 0;
    SetTick(200);
    mrtk_timer_hard_check();

    // Step 5: Then - 验证不再触发
    EXPECT_EQ(callback_count, 0) << "单次定时器不应再次触发";
}

/**
 * @test 重启定时器测试：已启动定时器再次调用 start
 * @brief Given 启动一个定时器
 *        When 再次调用 mrtk_timer_start
 *        Then 定时器应被移除后重新插入，超时时间重新计算
 * @note 分支覆盖：timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED 分支
 */
TEST_F(MrtkTimerTest, RestartTimer_ReinsertWithNewTimeout)
{
    // Step 1: Given - 初始化并启动定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "restart_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证第一次启动的 timeout_tick = 100
    EXPECT_EQ(test_timers[0].timeout_tick, 100) << "第一次启动 timeout_tick 应为 100";

    // Step 3: When - 推进到 tick = 50
    SetTick(50);

    // Step 4: When - 再次启动定时器（重启）
    ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 5: Then - 验证重启后的 timeout_tick = 150（50 + 100）
    EXPECT_EQ(test_timers[0].timeout_tick, 150) << "重启后 timeout_tick 应为 150";

    // Step 6: When - 推进到原超时点（tick = 100），不应触发
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 0) << "原超时点不应触发";

    // Step 7: When - 推进到新超时点（tick = 150），应触发
    SetTick(150);
    mrtk_timer_hard_check();

    // Step 8: Then - 验证触发
    EXPECT_EQ(callback_count, 1) << "新超时点应触发";
}

/* ==============================================================================
 * 测试用例：有序链表插入测试
 * ============================================================================== */

/**
 * @test 有序链表插入测试：验证升序排列
 * @brief Given 按乱序启动 3 个定时器（timeout: 300, 100, 200）
 *        When 所有定时器启动完成后
 *        Then 链表应按 timeout_tick 升序排列（100, 200, 300）
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, OrderedInsertion_SortedByTimeout)
{
    // Step 1: Given - 按乱序启动定时器
    SetTick(0);

    /* 第一个定时器：timeout = 300 */
    mrtk_timer_init(&test_timers[0], "timer_300", TestCallback, &test_timers[0], 300,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    /* 第二个定时器：timeout = 100 */
    mrtk_timer_init(&test_timers[1], "timer_100", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[1]);

    /* 第三个定时器：timeout = 200 */
    mrtk_timer_init(&test_timers[2], "timer_200", TestCallback, &test_timers[2], 200,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[2]);

    // Step 2: When - 验证链表长度
    EXPECT_EQ(GetHardTimerListLength(), 3) << "应有 3 个定时器";

    // Step 3: Then - 验证链表有序
    EXPECT_TRUE(VerifyHardTimerListOrder()) << "链表应按 timeout_tick 升序排列";

    // Step 4: Then - 验证链表顺序：100 -> 200 -> 300
    extern mrtk_list_t g_hard_timer_list;
    mrtk_timer_t *first  = _mrtk_list_entry(g_hard_timer_list.next, mrtk_timer_t, timer_node);
    mrtk_timer_t *second = _mrtk_list_entry(first->timer_node.next, mrtk_timer_t, timer_node);
    mrtk_timer_t *third  = _mrtk_list_entry(second->timer_node.next, mrtk_timer_t, timer_node);

    EXPECT_EQ(first->timeout_tick, 100) << "第一个节点 timeout 应为 100";
    EXPECT_EQ(second->timeout_tick, 200) << "第二个节点 timeout 应为 200";
    EXPECT_EQ(third->timeout_tick, 300) << "第三个节点 timeout 应为 300";
}

/**
 * @test 有序链表插入测试：相同超时时间按插入顺序排列
 * @brief Given 启动 3 个超时时间相同的定时器
 *        When 所有定时器启动完成后
 *        Then 先启动的定时器排在链表前面（FIFO）
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, OrderedInsertion_SameTimeoutPreservesInsertionOrder)
{
    // Step 1: Given - 启动 3 个超时时间相同的定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "timer_1", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    mrtk_timer_init(&test_timers[1], "timer_2", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[1]);

    mrtk_timer_init(&test_timers[2], "timer_3", TestCallback, &test_timers[2], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[2]);

    // Step 2: When - 验证链表顺序
    extern mrtk_list_t g_hard_timer_list;
    mrtk_timer_t *first  = _mrtk_list_entry(g_hard_timer_list.next, mrtk_timer_t, timer_node);
    mrtk_timer_t *second = _mrtk_list_entry(first->timer_node.next, mrtk_timer_t, timer_node);
    mrtk_timer_t *third  = _mrtk_list_entry(second->timer_node.next, mrtk_timer_t, timer_node);

    // Step 3: Then - 验证插入顺序
    EXPECT_EQ(first, &test_timers[0]) << "第一个应为 timer_1";
    EXPECT_EQ(second, &test_timers[1]) << "第二个应为 timer_2";
    EXPECT_EQ(third, &test_timers[2]) << "第三个应为 timer_3";
}

/* ==============================================================================
 * 测试用例：双链表隔离测试
 * ============================================================================== */

/**
 * @test 双链表隔离测试：硬定时器和软定时器独立管理
 * @brief Given 创建 3 个硬定时器（timeout: 200, 100, 300）和 2 个软定时器（timeout: 150, 50）
 *        When 启动所有定时器并调用检查函数
 *        Then 硬定时器和软定时器链表分别有序且互不干扰
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, DualListIsolation_HardAndSoftTimers)
{
    // Step 1: Given - 初始化定时器（故意打乱启动顺序以测试排序逻辑）
    SetTick(0);

    /* 硬定时器 1: timeout = 200 */
    mrtk_timer_init(&test_timers[0], "hard_timer_1", TestCallback, &test_timers[0], 200,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK) << "硬定时器 1 启动失败";

    /* 软定时器 1: timeout = 150 */
    mrtk_timer_init(&test_timers[1], "soft_timer_1", TestCallback, &test_timers[1], 150,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    ret = mrtk_timer_start(&test_timers[1]);
    ASSERT_EQ(ret, MRTK_EOK) << "软定时器 1 启动失败";

    /* 硬定时器 2: timeout = 100 */
    mrtk_timer_init(&test_timers[2], "hard_timer_2", TestCallback, &test_timers[2], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    ret = mrtk_timer_start(&test_timers[2]);
    ASSERT_EQ(ret, MRTK_EOK) << "硬定时器 2 启动失败";

    /* 软定时器 2: timeout = 50 */
    mrtk_timer_init(&test_timers[3], "soft_timer_2", TestCallback, &test_timers[3], 50,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    ret = mrtk_timer_start(&test_timers[3]);
    ASSERT_EQ(ret, MRTK_EOK) << "软定时器 2 启动失败";

    /* 硬定时器 3: timeout = 300 */
    mrtk_timer_init(&test_timers[4], "hard_timer_3", TestCallback, &test_timers[4], 300,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    ret = mrtk_timer_start(&test_timers[4]);
    ASSERT_EQ(ret, MRTK_EOK) << "硬定时器 3 启动失败";

    // Step 2: When - 验证硬定时器链表长度
    EXPECT_EQ(GetHardTimerListLength(), 3) << "硬定时器链表应有 3 个定时器";

    // Step 3: When - 验证软定时器链表长度
    EXPECT_EQ(GetSoftTimerListLength(), 2) << "软定时器链表应有 2 个定时器";

    // Step 4: Then - 验证硬定时器链表有序（100 -> 200 -> 300）
    EXPECT_TRUE(VerifyHardTimerListOrder()) << "硬定时器链表应按 timeout_tick 升序排列";

    // Step 5: Then - 验证软定时器链表有序（50 -> 150）
    EXPECT_TRUE(VerifySoftTimerListOrder()) << "软定时器链表应按 timeout_tick 升序排列";

    // Step 6: Then - 验证硬定时器 timeout_tick 值
    EXPECT_EQ(test_timers[2].timeout_tick, 100) << "hard_timer_2 timeout_tick 应为 100";
    EXPECT_EQ(test_timers[0].timeout_tick, 200) << "hard_timer_1 timeout_tick 应为 200";
    EXPECT_EQ(test_timers[4].timeout_tick, 300) << "hard_timer_3 timeout_tick 应为 300";

    // Step 7: Then - 验证软定时器 timeout_tick 值
    EXPECT_EQ(test_timers[3].timeout_tick, 50) << "soft_timer_2 timeout_tick 应为 50";
    EXPECT_EQ(test_timers[1].timeout_tick, 150) << "soft_timer_1 timeout_tick 应为 150";
}

/**
 * @test 双链表隔离测试：hard_check 不影响软定时器
 * @brief Given 硬定时器和软定时器各一个
 *        When 只调用硬定时器检查函数
 *        Then 只触发硬定时器，软定时器不受影响
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, DualListIsolation_HardCheckNoAffectSoft)
{
    // Step 1: Given - 硬定时器和软定时器各一个
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "hard_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    mrtk_timer_init(&test_timers[1], "soft_timer", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_timer_start(&test_timers[1]);

    mrtk_u32_t soft_timeout = test_timers[1].timeout_tick;

    // Step 2: When - 只调用硬定时器检查
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 3: Then - 验证只触发硬定时器
    EXPECT_EQ(callback_count, 1) << "应只触发硬定时器";
    EXPECT_EQ(GetHardTimerListLength(), 0) << "硬定时器应已触发并移除";
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "软定时器未触发";
    EXPECT_EQ(test_timers[1].timeout_tick, soft_timeout) << "软定时器超时时间未变";
}

/**
 * @test 双链表隔离测试：soft_check 不影响硬定时器
 * @brief Given 硬定时器和软定时器各一个
 *        When 只调用软定时器检查函数
 *        Then 只触发软定时器，硬定时器不受影响
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, DualListIsolation_SoftCheckNoAffectHard)
{
    // Step 1: Given - 硬定时器和软定时器各一个
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "hard_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    mrtk_timer_init(&test_timers[1], "soft_timer", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_timer_start(&test_timers[1]);

    mrtk_u32_t hard_timeout = test_timers[0].timeout_tick;

    // Step 2: When - 只调用软定时器检查
    SetTick(100);
    mrtk_timer_soft_check();

    // Step 3: Then - 验证只触发软定时器
    EXPECT_EQ(callback_count, 1) << "应只触发软定时器";
    EXPECT_EQ(GetSoftTimerListLength(), 0) << "软定时器应已触发并移除";
    EXPECT_EQ(GetHardTimerListLength(), 1) << "硬定时器未触发";
    EXPECT_EQ(test_timers[0].timeout_tick, hard_timeout) << "硬定时器超时时间未变";
}

/* ==============================================================================
 * 测试用例：32 位 tick 回卷测试
 * ============================================================================== */

/**
 * @test 32 位回卷测试：定时器跨越 0xFFFFFFFF 边界
 * @brief Given 当前 tick 设置为 0xFFFFFFF0，启动超时为 100 的定时器
 *        When 推进 tick 到 0x00000054（跨越回卷边界）
 *        Then 定时器应正确触发，回调函数被调用
 * @note 边界值分析：UINT32_MAX 边界
 * @note 分支覆盖：!(cur_tick - first_timer->timeout_tick < MRTK_TICK_MAX / 2) 分支
 */
TEST_F(MrtkTimerTest, TickWraparound_TimerTriggersCorrectly)
{
    // Step 1: Given - 设置当前 tick 接近 32 位最大值
    SetTick(0xFFFFFFF0);

    // Step 2: When - 初始化并启动硬定时器，timeout = 100
    mrtk_timer_init(&test_timers[0], "wraparound_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK) << "回卷测试定时器启动失败";

    // Step 3: Then - 验证定时器 timeout_tick 正确计算（0xFFFFFFF0 + 100 = 0x00000054）
    EXPECT_EQ(test_timers[0].timeout_tick, 0x00000054)
        << "timeout_tick 应正确回卷到 0x00000054";

    // Step 4: When - 推进 tick 到 0x00000040（未到达 timeout）
    SetTick(0x00000040);
    mrtk_timer_hard_check();

    // Step 5: Then - 验证定时器未触发
    EXPECT_EQ(callback_count, 0) << "tick=0x00000040 时定时器不应触发";

    // Step 6: When - 推进 tick 到 0x00000054（到达 timeout）
    SetTick(0x00000054);
    mrtk_timer_hard_check();

    // Step 7: Then - 验证定时器触发
    EXPECT_EQ(callback_count, 1) << "tick=0x00000054 时定时器应触发";
    EXPECT_EQ(last_callback_timer, &test_timers[0]) << "回调应为正确的定时器";
}

/**
 * @test 32 位回卷边界测试：最大阈值验证
 * @brief Given 测试回卷算法的边界条件（MRTK_TICK_MAX / 2）
 *        When 验证无符号减法判断逻辑
 *        Then 回卷处理算法应正确识别超时
 * @note 边界值分析：MRTK_TICK_MAX / 2 边界
 */
TEST_F(MrtkTimerTest, TickWraparound_BoundaryThresholdTest)
{
    // Step 1: Given - 测试正常情况（未回卷）
    SetTick(1000);

    mrtk_timer_init(&test_timers[0], "boundary_timer_1", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // 推进到 1099（未超时）
    SetTick(1099);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 0) << "未到达 timeout 不应触发";

    // 推进到 1100（刚好超时）
    SetTick(1100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "到达 timeout 应触发";

    // 验证第一个定时器已从链表中移除
    EXPECT_EQ(GetHardTimerListLength(), 0) << "第一个定时器触发后应从链表中移除";

    // Step 2: Given - 测试回卷边界
    callback_count = 0;
    SetTick(0xFFFFFFFE);

    mrtk_timer_init(&test_timers[1], "boundary_timer_2", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    /* timeout = 0xFFFFFFFE + 100 = 0x00000062 (98 in decimal, 回卷) */
    ret = mrtk_timer_start(&test_timers[1]);
    ASSERT_EQ(ret, MRTK_EOK);

    /* 诊断：验证定时器已正确插入链表 */
    ASSERT_EQ(GetHardTimerListLength(), 1) << "启动定时器后链表中应有 1 个定时器";
    EXPECT_EQ(test_timers[1].timeout_tick, 98) << "timeout_tick 应为 98 (0x62)";

    // 推进到 0x00000061（97，未超时）
    SetTick(0x00000061);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 0) << "回卷后未到达 timeout 不应触发";

    // 推进到 0x00000062（98，刚好超时）
    SetTick(0x00000062);

    /* 诊断：检查定时器状态 */
    EXPECT_EQ(mrtk_tick_get(), 98) << "当前 tick 应为 98";
    EXPECT_EQ(test_timers[1].timeout_tick, 98) << "timeout_tick 应为 98";
    EXPECT_EQ(test_timers[1].obj.flag & MRTK_TIMER_FLAG_ACTIVATED, MRTK_TIMER_FLAG_ACTIVATED)
        << "定时器应为激活状态";
    EXPECT_EQ(GetHardTimerListLength(), 1) << "触发前链表中应有 1 个定时器";

    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "回卷后到达 timeout 应触发";
}

/**
 * @test 32 位回卷周期定时器测试
 * @brief Given 启动一个周期定时器，当前 tick 接近回卷边界
 *        When 周期定时器跨越回卷边界触发并重启
 *        Then 重启后的 timeout_tick 应正确计算
 * @note 边界值分析：回卷边界
 * @note 分支覆盖：first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC 分支
 */
TEST_F(MrtkTimerTest, TickWraparound_PeriodicTimerRestartsCorrectly)
{
    // Step 1: Given - 设置 tick 接近回卷边界
    SetTick(0xFFFFFFF0);

    // Step 2: When - 启动周期定时器，period = 100
    mrtk_timer_init(&test_timers[0], "wraparound_periodic", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_PERIODIC);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 3: Then - 验证初始 timeout_tick = 0x00000054
    EXPECT_EQ(test_timers[0].timeout_tick, 0x00000054) << "初始 timeout_tick 应为 0x00000054";

    // Step 4: When - 推进到第一次超时
    SetTick(0x00000054);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第一次应触发";

    // Step 5: Then - 验证重启后的 timeout_tick = 0x000000B8（0x54 + 100 == 0xB8）
    EXPECT_EQ(test_timers[0].timeout_tick, 0x000000B8)
        << "重启后 timeout_tick 应为 0x000000B8（回卷后正确累加）";
}

/* =============================================================================
 * 测试用例：周期定时器无漂移测试
 * ============================================================================ */

/**
 * @test 周期定时器无漂移测试：使用加法而非模运算
 * @brief Given 启动周期为 1000 的周期定时器，当前 tick 为 0
 *        When 推进 tick 到 1050 后触发定时器
 *        Then 重启后的 timeout_tick 应为 2000，而非 2050（无漂移）
 * @note 分支覆盖：first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC 分支
 */
TEST_F(MrtkTimerTest, PeriodicTimerNoDrift_AdditionAlgorithm)
{
    // Step 1: Given - 初始化周期定时器，period = 1000
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "periodic_timer", TestCallback, &test_timers[0], 1000,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_PERIODIC);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK) << "周期定时器启动失败";

    // Step 2: When - 验证初始 timeout_tick = 1000
    EXPECT_EQ(test_timers[0].timeout_tick, 1000) << "初始 timeout_tick 应为 1000";

    // Step 3: When - 推进 tick 到 1050（超时 50 ticks）
    SetTick(1050);

    // Step 4: When - 调用硬定时器检查函数
    mrtk_timer_hard_check();

    // Step 5: Then - 验证回调被调用
    EXPECT_EQ(callback_count, 1) << "定时器应触发一次";

    // Step 6: Then - 验证重启后的 timeout_tick = 2000（而非 2050）
    EXPECT_EQ(test_timers[0].timeout_tick, 2000)
        << "重启后的 timeout_tick 应为 2000（无漂移）";

    // Step 7: When - 推进 tick 到 2050（再次超时 50 ticks）
    SetTick(2050);
    callback_count = 0;

    mrtk_timer_hard_check();

    // Step 8: Then - 验证回调再次被调用
    EXPECT_EQ(callback_count, 1) << "定时器应第二次触发";

    // Step 9: Then - 验证重启后的 timeout_tick = 3000（而非 3050）
    EXPECT_EQ(test_timers[0].timeout_tick, 3000)
        << "第二次重启后的 timeout_tick 应为 3000（无漂移）";
}

/**
 * @test 周期定时器无漂移验证测试：多周期累积
 * @brief Given 启动周期为 100 的周期定时器
 *        When 连续触发 10 次，每次有 5 ticks 的延迟
 *        Then 第 10 次触发后 timeout_tick 应为 1000，无累积漂移
 * @note 分支覆盖：first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC 分支
 */
TEST_F(MrtkTimerTest, PeriodicTimerNoDrift_MultiplePeriods)
{
    // Step 1: Given - 初始化周期定时器，period = 100
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "multi_period_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_PERIODIC);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 模拟 10 次周期触发，每次延迟 5 ticks
    for (int i = 1; i <= 10; ++i) {
        // 推进到超时后 5 ticks
        mrtk_u32_t expected_tick = i * 100 + 5;
        SetTick(expected_tick);

        // 重置计数器
        callback_count = 0;

        // 触发定时器检查
        mrtk_timer_hard_check();

        // 验证触发
        EXPECT_EQ(callback_count, 1) << "第 " << i << " 次应触发";

        // 验证 timeout_tick = (i + 1) * 100（无漂移）
        mrtk_u32_t expected_timeout = (i + 1) * 100;
        EXPECT_EQ(test_timers[0].timeout_tick, expected_timeout)
            << "第 " << i << " 次重启后 timeout_tick 应为 " << expected_timeout << "（无漂移）";
    }

    // Step 3: Then - 验证最终 timeout_tick = 1100
    EXPECT_EQ(test_timers[0].timeout_tick, 1100) << "10 次触发后 timeout_tick 应为 1100";
}

/* =============================================================================
 * 测试用例：临界区最小化测试
 * ============================================================================ */

/**
 * @test 临界区最小化测试：回调执行时中断应使能
 * @brief Given 启动一个硬定时器
 *        When 定时器超时并执行回调函数
 *        Then 回调执行期间中断应为使能状态（非临界区内）
 * @note 分支覆盖：mrtk_hw_interrupt_enable 在回调前的调用
 */
TEST_F(MrtkTimerTest, CriticalSectionMinimization_InterruptEnabledDuringCallback)
{
    // Step 1: Given - 初始化定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "irq_test_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 设置期望：在 mrtk_timer_hard_check 调用期间，中断会被使能
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::_))
        .Times(testing::AtLeast(1))
        .WillOnce(testing::Return())
        .RetiresOnSaturation();

    // Step 3: When - 推进 tick 到超时点
    SetTick(100);

    // Step 4: When - 调用硬定时器检查函数（应触发回调）
    mrtk_timer_hard_check();

    // Step 5: Then - 验证回调被调用
    EXPECT_EQ(callback_count, 1) << "定时器应触发";
}

/**
 * @test 临界区最小化测试：软定时器回调执行时中断应使能
 * @brief Given 启动一个软定时器
 *        When 定时器超时并执行回调函数
 *        Then 回调执行期间中断应为使能状态
 * @note 分支覆盖：mrtk_hw_interrupt_enable 在回调前的调用
 */
TEST_F(MrtkTimerTest, CriticalSectionMinimization_SoftTimerInterruptEnabled)
{
    // Step 1: Given - 初始化软定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "soft_irq_test_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 设置期望：在 mrtk_timer_soft_check 调用期间，中断会被使能
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::_))
        .Times(testing::AtLeast(1))
        .WillOnce(testing::Return())
        .RetiresOnSaturation();

    // Step 3: When - 推进 tick 到超时点
    SetTick(100);

    // Step 4: When - 调用软定时器检查函数（应触发回调）
    mrtk_timer_soft_check();

    // Step 5: Then - 验证回调被调用
    EXPECT_EQ(callback_count, 1) << "软定时器应触发";
}

/* =============================================================================
 * 测试用例：定时器控制功能测试
 * ============================================================================ */

/**
 * @test 定时器控制功能：获取初始超时时间
 * @brief Given 启动一个超时为 100 的定时器
 *        When 使用 MRTK_TIMER_CMD_GET_TIME 查询
 *        Then 应返回初始超时时间 100（非剩余时间）
 * @note 分支覆盖：MRTK_TIMER_CMD_GET_TIME 分支
 */
TEST_F(MrtkTimerTest, TimerControl_GetInitTime)
{
    // Step 1: Given - 初始化并启动定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "control_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 推进到 tick = 30
    SetTick(30);

    // Step 3: When - 查询初始超时时间
    mrtk_u32_t init_time = 0;
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_GET_TIME, &init_time);

    // Step 4: Then - 验证返回的是初始超时时间（不是剩余时间）
    EXPECT_EQ(ret, MRTK_EOK) << "获取初始时间应成功";
    EXPECT_EQ(init_time, 100) << "MRTK_TIMER_CMD_GET_TIME 返回 init_tick，应为 100";
}

/**
 * @test 定时器控制功能：SET_TIME 命令
 * @brief Given 启动一个超时为 100 的定时器
 *        When 使用 MRTK_TIMER_CMD_SET_TIME 修改为 50
 *        Then init_tick 应被修改为 50
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_TIME 分支
 */
TEST_F(MrtkTimerTest, TimerControl_SetTime)
{
    // Step 1: Given - 初始化并启动定时器，init_tick = 100
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "set_time_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(test_timers[0].init_tick, 100) << "初始 init_tick 应为 100";

    // Step 2: When - 修改 init_tick 为 50
    mrtk_u32_t new_time = 50;
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_TIME, &new_time);

    // Step 3: Then - 验证 init_tick 被修改
    EXPECT_EQ(ret, MRTK_EOK) << "设置时间应成功";
    EXPECT_EQ(test_timers[0].init_tick, 50) << "init_tick 应被修改为 50";
}

/**
 * @test 定时器控制功能：SET_TIME 命令（NULL 参数）
 * @brief Given 启动一个定时器
 *        When 使用 MRTK_TIMER_CMD_SET_TIME 传入 NULL 参数
 *        Then 应返回 MRTK_EINVAL
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：arg == MRTK_NULL 分支（SET_TIME）
 */
TEST_F(MrtkTimerTest, TimerControl_SetTime_NullArg)
{
    // Step 1: Given - 初始化定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "set_time_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    // Step 2: When - 使用 NULL 参数执行 SET_TIME
    mrtk_err_t ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_TIME, nullptr);

    // Step 3: Then - 应返回错误
    EXPECT_EQ(ret, MRTK_EINVAL) << "SET_TIME 命令传入 NULL 参数应返回 MRTK_EINVAL";
}

/**
 * @test 定时器控制功能：GET_TIME 命令（NULL 参数）
 * @brief Given 启动一个定时器
 *        When 使用 MRTK_TIMER_CMD_GET_TIME 传入 NULL 参数
 *        Then 应返回 MRTK_EINVAL
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：arg == MRTK_NULL 分支（GET_TIME）
 */
TEST_F(MrtkTimerTest, TimerControl_GetTime_NullArg)
{
    // Step 1: Given - 初始化定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "get_time_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    // Step 2: When - 使用 NULL 参数执行 GET_TIME
    mrtk_err_t ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_GET_TIME, nullptr);

    // Step 3: Then - 应返回错误
    EXPECT_EQ(ret, MRTK_EINVAL) << "GET_TIME 命令传入 NULL 参数应返回 MRTK_EINVAL";
}

/**
 * @test 定时器控制功能：设置周期模式
 * @brief Given 启动一个超时为 100 的单次定时器
 *        When 使用 MRTK_TIMER_CMD_SET_PERIODIC 修改为周期定时器
 *        Then init_tick 保持不变（仍是 100），周期使用原始 init_tick
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_PERIODIC 分支
 */
TEST_F(MrtkTimerTest, TimerControl_SetPeriodic)
{
    // Step 1: Given - 初始化单次定时器，init_tick = 100
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "set_periodic_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 修改为周期定时器（传入参数被忽略，仅设置标志位）
    mrtk_u32_t period = 50;  /* 这个值会被忽略 */
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_PERIODIC, &period);

    // Step 3: Then - 验证修改成功，但 init_tick 仍为 100
    EXPECT_EQ(ret, MRTK_EOK) << "设置周期标志应成功";
    EXPECT_EQ(test_timers[0].init_tick, 100) << "init_tick 应保持不变";

    // Step 4: When - 推进到第一次超时（tick = 100）
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第一次应触发";

    // Step 5: When - 推进到第二次超时（tick = 200 = 100 + 100，使用原始 init_tick）
    SetTick(200);
    callback_count = 0;
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第二次应触发，周期使用原始 init_tick=100";
}

/**
 * @test 定时器控制功能：修改周期时间
 * @brief Given 启动一个超时为 100 的单次定时器
 *        When 使用 MRTK_TIMER_CMD_SET_TIME 修改 init_tick 为 50，并设置为周期模式
 *        Then 周期定时器使用新的 init_tick = 50
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_TIME + MRTK_TIMER_CMD_SET_PERIODIC 分支
 */
TEST_F(MrtkTimerTest, TimerControl_ChangePeriod)
{
    // Step 1: Given - 初始化单次定时器，init_tick = 100
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "change_period_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 修改 init_tick 为 50 并设置为周期模式
    mrtk_u32_t new_period = 50;
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_TIME, &new_period);
    EXPECT_EQ(ret, MRTK_EOK) << "修改周期应成功";

    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_PERIODIC, &new_period);
    EXPECT_EQ(ret, MRTK_EOK) << "设置周期标志应成功";

    // Step 3: Then - 验证 init_tick 已被修改为 50
    EXPECT_EQ(test_timers[0].init_tick, 50) << "init_tick 应被修改为 50";

    // Step 4: When - 推进到第一次超时（tick = 100，使用启动时的 init_tick）
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第一次应触发";

    // Step 5: When - 推进到第二次超时（tick = 150 = 100 + 50，使用新的 init_tick）
    SetTick(150);
    callback_count = 0;
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第二次应触发，使用修改后的 init_tick=50";
}

/**
 * @test 定时器控制功能：修改为单次模式
 * @brief Given 启动一个周期定时器
 *        When 使用 MRTK_TIMER_CMD_SET_ONESHOT 修改为单次模式
 *        Then 定时器触发后不再重启
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_ONESHOT 分支
 */
TEST_F(MrtkTimerTest, TimerControl_SetOneshot)
{
    // Step 1: Given - 初始化周期定时器，init_tick = 100
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "set_oneshot_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_PERIODIC);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 修改为单次模式
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_ONESHOT, nullptr);
    EXPECT_EQ(ret, MRTK_EOK) << "设置为单次模式应成功";

    // Step 3: Then - 验证周期标志已清除
    EXPECT_EQ(test_timers[0].obj.flag & MRTK_TIMER_FLAG_PERIODIC, 0)
        << "周期标志应被清除";

    // Step 4: When - 推进到第一次超时
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "第一次应触发";

    // Step 5: Then - 验证定时器已从链表中移除（单次模式不重启）
    EXPECT_EQ(GetHardTimerListLength(), 0) << "单次定时器触发后应从链表移除";
}

/**
 * @test 定时器控制功能：硬定时器切换为软定时器
 * @brief Given 启动一个硬定时器
 *        When 使用 MRTK_TIMER_CMD_SET_SOFT_MODE 修改为软定时器
 *        Then 定时器应从硬定时器链表移动到软定时器链表
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_SOFT_MODE 分支
 * @note 分支覆盖：was_activated 分支
 */
TEST_F(MrtkTimerTest, TimerControl_SwitchFromHardToSoft)
{
    // Step 1: Given - 初始化硬定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "hard_to_soft_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证定时器在硬定时器链表中
    EXPECT_EQ(GetHardTimerListLength(), 1) << "应在硬定时器链表中";
    EXPECT_EQ(GetSoftTimerListLength(), 0) << "不应在软定时器链表中";

    // Step 3: When - 切换为软定时器模式
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_SOFT_MODE, nullptr);
    EXPECT_EQ(ret, MRTK_EOK) << "切换为软定时器应成功";

    // Step 4: Then - 验证定时器已移动到软定时器链表
    EXPECT_EQ(GetHardTimerListLength(), 0) << "硬定时器链表应为空";
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "应在软定时器链表中";

    // Step 5: Then - 验证软定时器标志已设置
    EXPECT_TRUE(test_timers[0].obj.flag & MRTK_TIMER_FLAG_SOFT_TIMER)
        << "软定时器标志应已设置";

    // Step 6: When - 推进到超时点并验证软定时器触发
    SetTick(100);
    mrtk_timer_soft_check();
    EXPECT_EQ(callback_count, 1) << "软定时器应触发";
}

/**
 * @test 定时器控制功能：软定时器切换为硬定时器
 * @brief Given 启动一个软定时器
 *        When 使用 MRTK_TIMER_CMD_SET_HARD_MODE 修改为硬定时器
 *        Then 定时器应从软定时器链表移动到硬定时器链表
 * @note 分支覆盖：MRTK_TIMER_CMD_SET_HARD_MODE 分支
 * @note 分支覆盖：was_activated 分支
 */
TEST_F(MrtkTimerTest, TimerControl_SwitchFromSoftToHard)
{
    // Step 1: Given - 初始化软定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "soft_to_hard_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证定时器在软定时器链表中
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "应在软定时器链表中";
    EXPECT_EQ(GetHardTimerListLength(), 0) << "不应在硬定时器链表中";

    // Step 3: When - 切换为硬定时器模式
    ret = mrtk_timer_control(&test_timers[0], MRTK_TIMER_CMD_SET_HARD_MODE, nullptr);
    EXPECT_EQ(ret, MRTK_EOK) << "切换为硬定时器应成功";

    // Step 4: Then - 验证定时器已移动到硬定时器链表
    EXPECT_EQ(GetSoftTimerListLength(), 0) << "软定时器链表应为空";
    EXPECT_EQ(GetHardTimerListLength(), 1) << "应在硬定时器链表中";

    // Step 5: Then - 验证软定时器标志已清除
    EXPECT_FALSE(test_timers[0].obj.flag & MRTK_TIMER_FLAG_SOFT_TIMER)
        << "软定时器标志应已清除";

    // Step 6: When - 推进到超时点并验证硬定时器触发
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "硬定时器应触发";
}

/**
 * @test 定时器控制功能：未知命令
 * @brief Given 启动一个定时器
 *        When 使用未知命令调用 mrtk_timer_control
 *        Then 应返回 MRTK_EINVAL
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：default 分支
 */
TEST_F(MrtkTimerTest, TimerControl_UnknownCommand)
{
    // Step 1: Given - 初始化定时器
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "unknown_cmd_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    // Step 2: When - 使用未知命令
    mrtk_err_t ret = mrtk_timer_control(&test_timers[0], 0xFF, nullptr);

    // Step 3: Then - 应返回错误
    EXPECT_EQ(ret, MRTK_EINVAL) << "未知命令应返回 MRTK_EINVAL";
}

/* =============================================================================
 * 测试用例：定时器停止和删除测试
 * ============================================================================ */

/**
 * @test 定时器停止功能测试
 * @brief Given 启动一个定时器
 *        When 在超时前停止定时器
 *        Then 定时器不应触发，且从链表中移除
 * @note 状态机覆盖：Running -> Stop
 * @note 分支覆盖：!(timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) 分支
 */
TEST_F(MrtkTimerTest, TimerStop_PreventCallback)
{
    // Step 1: Given - 初始化并启动定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "stop_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证定时器在链表中
    EXPECT_EQ(GetHardTimerListLength(), 1) << "启动后定时器应在链表中";

    // Step 3: When - 在超时前停止定时器
    SetTick(50);
    ret = mrtk_timer_stop(&test_timers[0]);
    EXPECT_EQ(ret, MRTK_EOK) << "停止定时器应成功";

    // Step 4: Then - 验证定时器从链表中移除
    EXPECT_EQ(GetHardTimerListLength(), 0) << "停止后定时器应从链表中移除";

    // Step 5: When - 推进到原超时点
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 6: Then - 验证回调未触发
    EXPECT_EQ(callback_count, 0) << "停止后的定时器不应触发";
}

/**
 * @test 停止未启动的定时器测试
 * @brief Given 初始化一个定时器但不启动
 *        When 调用 mrtk_timer_stop
 *        Then 应返回 MRTK_ERROR
 * @note 状态机覆盖：Init -> Stop（非法状态）
 * @note 分支覆盖：!(timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) 分支
 */
TEST_F(MrtkTimerTest, TimerStop_NotStartedReturnsError)
{
    // Step 1: Given - 初始化定时器但不启动
    SetTick(0);
    mrtk_timer_init(&test_timers[0], "not_started_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);

    // Step 2: When - 尝试停止未启动的定时器
    mrtk_err_t ret = mrtk_timer_stop(&test_timers[0]);

    // Step 3: Then - 验证返回错误
    EXPECT_EQ(ret, MRTK_ERROR) << "停止未启动的定时器应返回 MRTK_ERROR";
}

/**
 * @test 定时器分离功能测试
 * @brief Given 启动一个定时器
 *        When 分离定时器（mrtk_timer_detach）
 *        Then 定时器应从全局对象链表中移除，且不会触发
 * @note 状态机覆盖：Running -> Detach -> Deleted
 */
TEST_F(MrtkTimerTest, TimerDetach_RemoveFromGlobalList)
{
    // Step 1: Given - 初始化并启动定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "detach_timer", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_err_t ret = mrtk_timer_start(&test_timers[0]);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 2: When - 验证定时器已启动
    EXPECT_TRUE(test_timers[0].obj.flag & MRTK_TIMER_FLAG_ACTIVATED) << "定时器应已启动";

    // Step 3: When - 分离定时器
    ret = mrtk_timer_detach(&test_timers[0]);
    EXPECT_EQ(ret, MRTK_EOK) << "分离定时器应成功";

    // Step 4: Then - 验证定时器已停止
    EXPECT_FALSE(test_timers[0].obj.flag & MRTK_TIMER_FLAG_ACTIVATED) << "定时器应已停止";

    // Step 5: When - 推进到超时点
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 6: Then - 验证回调未触发
    EXPECT_EQ(callback_count, 0) << "分离后的定时器不应触发";
}

/**
 * @test 定时器动态创建和删除测试
 * @brief Given 动态创建一个定时器
 *        When 删除定时器
 *        Then 定时器应被正确释放，不会触发
 * @note 状态机覆盖：Create -> Start -> Delete
 */
TEST_F(MrtkTimerTest, DynamicCreateAndDelete)
{
    // Step 1: Given - 动态创建定时器
    SetTick(0);
    mrtk_timer_t *dynamic_timer = mrtk_timer_create("dynamic_timer", TestCallback, nullptr, 100,
                                                     MRTK_TIMER_FLAG_HARD_TIMER);
    ASSERT_NE(dynamic_timer, nullptr) << "动态创建定时器应成功";

    // Step 2: When - 启动定时器
    mrtk_err_t ret = mrtk_timer_start(dynamic_timer);
    ASSERT_EQ(ret, MRTK_EOK);

    // Step 3: Then - 验证定时器在链表中
    EXPECT_EQ(GetHardTimerListLength(), 1) << "动态定时器应在链表中";

    // Step 4: When - 删除定时器
    ret = mrtk_timer_delete(dynamic_timer);
    EXPECT_EQ(ret, MRTK_EOK) << "删除定时器应成功";

    // Step 5: Then - 验证定时器从链表中移除
    EXPECT_EQ(GetHardTimerListLength(), 0) << "删除后定时器应从链表移除";

    // Step 6: When - 推进到超时点
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 7: Then - 验证回调未触发
    EXPECT_EQ(callback_count, 0) << "删除后的定时器不应触发";
}

/* =============================================================================
 * 测试用例：混合硬定时器和软定时器测试
 * ============================================================================ */

/**
 * @test 混合硬定时器和软定时器测试：独立触发
 * @brief Given 启动 2 个硬定时器和 2 个软定时器，超时时间交错
 *        When 分别调用硬定时器检查和软定时器检查
 *        Then 硬定时器和软定时器应独立触发，互不干扰
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, MixedHardAndSoftTimers_FireIndependently)
{
    // Step 1: Given - 启动硬定时器和软定时器
    SetTick(0);

    /* 硬定时器 1: timeout = 100 */
    mrtk_timer_init(&test_timers[0], "hard_1", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    /* 软定时器 1: timeout = 150 */
    mrtk_timer_init(&test_timers[1], "soft_1", TestCallback, &test_timers[1], 150,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_timer_start(&test_timers[1]);

    /* 硬定时器 2: timeout = 200 */
    mrtk_timer_init(&test_timers[2], "hard_2", TestCallback, &test_timers[2], 200,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[2]);

    /* 软定时器 2: timeout = 250 */
    mrtk_timer_init(&test_timers[3], "soft_2", TestCallback, &test_timers[3], 250,
                    MRTK_TIMER_FLAG_HARD_TIMER | MRTK_TIMER_FLAG_SOFT_TIMER);
    mrtk_timer_start(&test_timers[3]);

    // Step 2: When - 推进到 tick = 100，只触发硬定时器 1
    SetTick(100);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "硬定时器 1 应触发";
    EXPECT_EQ(GetHardTimerListLength(), 1) << "硬定时器链表应剩 1 个";
    EXPECT_EQ(GetSoftTimerListLength(), 2) << "软定时器链表应仍为 2 个";

    // Step 3: When - 推进到 tick = 150，只触发软定时器 1
    callback_count = 0;
    SetTick(150);
    mrtk_timer_soft_check();
    EXPECT_EQ(callback_count, 1) << "软定时器 1 应触发";
    EXPECT_EQ(GetHardTimerListLength(), 1) << "硬定时器链表应仍为 1 个";
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "软定时器链表应剩 1 个";

    // Step 4: When - 推进到 tick = 200，只触发硬定时器 2
    callback_count = 0;
    SetTick(200);
    mrtk_timer_hard_check();
    EXPECT_EQ(callback_count, 1) << "硬定时器 2 应触发";
    EXPECT_EQ(GetHardTimerListLength(), 0) << "硬定时器链表应为空";
    EXPECT_EQ(GetSoftTimerListLength(), 1) << "软定时器链表应仍为 1 个";

    // Step 5: When - 推进到 tick = 250，只触发软定时器 2
    callback_count = 0;
    SetTick(250);
    mrtk_timer_soft_check();
    EXPECT_EQ(callback_count, 1) << "软定时器 2 应触发";
    EXPECT_EQ(GetHardTimerListLength(), 0) << "硬定时器链表应为空";
    EXPECT_EQ(GetSoftTimerListLength(), 0) << "软定时器链表应为空";
}

/* =============================================================================
 * 测试用例：大量定时器压力测试
 * ============================================================================ */

/**
 * @test 大量定时器压力测试
 * @brief Given 启动 50 个不同超时时间的硬定时器（使用局部数组）
 *        When 验证链表有序性和所有定时器正确触发
 *        Then 链表应保持有序，所有定时器按顺序触发
 * @note 压力测试：检测链表损坏和性能
 */
TEST_F(MrtkTimerTest, StressTest_MultipleTimersMaintainOrder)
{
    // Step 1: Given - 在栈上分配 50 个定时器控制块（局部变量，自动管理内存）
    mrtk_timer_t stress_timers[50];
    SetTick(0);

    // Step 2: When - 启动 50 个定时器，超时时间递增
    for (int i = 0; i < 50; ++i) {
        // 为每个定时器生成唯一名称
        char name[32];
        snprintf(name, sizeof(name), "stress_timer_%d", i);

        // 初始化并启动定时器
        mrtk_timer_init(&stress_timers[i], name, TestCallback, &stress_timers[i],
                        100 + i * 10, MRTK_TIMER_FLAG_HARD_TIMER);
        mrtk_err_t ret = mrtk_timer_start(&stress_timers[i]);
        ASSERT_EQ(ret, MRTK_EOK) << "第 " << i << " 个定时器启动失败";
    }

    // Step 3: Then - 验证链表中有 50 个定时器
    EXPECT_EQ(GetHardTimerListLength(), 50) << "应有 50 个定时器在链表中";

    // Step 4: Then - 验证链表有序（按 timeout_tick 升序）
    EXPECT_TRUE(VerifyHardTimerListOrder()) << "链表应按 timeout_tick 升序排列";

    // Step 5: When - 逐个触发前 10 个定时器，验证按顺序触发
    for (int i = 0; i < 10; ++i) {
        callback_count = 0;
        SetTick(100 + i * 10);
        mrtk_timer_hard_check();
        EXPECT_EQ(callback_count, 1) << "第 " << i << " 个定时器应触发";
        EXPECT_EQ(last_callback_timer, &stress_timers[i]) << "回调应为正确的定时器";
        EXPECT_EQ(GetHardTimerListLength(), 49 - i) << "剩余定时器数量应为 " << (49 - i);
    }

    // Step 6: When - 验证所有定时器都能触发（推进到最后一个超时点）
    SetTick(100 + 49 * 10);
    mrtk_timer_hard_check();

    // Step 7: Then - 验证所有定时器都已触发，链表为空
    EXPECT_EQ(GetHardTimerListLength(), 0) << "所有定时器触发后链表应为空";

    // 注意：stress_timers 是局部变量，自动清理，无需手动 delete
}

/**
 * @test 多定时器同时触发测试
 * @brief Given 启动 3 个超时时间相同的定时器
 *        When 推进到超时点
 *        Then 所有定时器都应按顺序触发
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkTimerTest, MultipleTimersWithSameTimeout_AllFire)
{
    // Step 1: Given - 启动 3 个超时时间相同的定时器
    SetTick(0);

    mrtk_timer_init(&test_timers[0], "same_timeout_1", TestCallback, &test_timers[0], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[0]);

    mrtk_timer_init(&test_timers[1], "same_timeout_2", TestCallback, &test_timers[1], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[1]);

    mrtk_timer_init(&test_timers[2], "same_timeout_3", TestCallback, &test_timers[2], 100,
                    MRTK_TIMER_FLAG_HARD_TIMER);
    mrtk_timer_start(&test_timers[2]);

    // Step 2: When - 验证链表中有 3 个定时器
    EXPECT_EQ(GetHardTimerListLength(), 3) << "应有 3 个定时器";

    // Step 3: When - 推进到超时点
    SetTick(100);
    mrtk_timer_hard_check();

    // Step 4: Then - 验证所有 3 个定时器都触发了
    EXPECT_EQ(callback_count, 3) << "所有 3 个定时器都应触发";

    // Step 5: Then - 验证链表为空
    EXPECT_EQ(GetHardTimerListLength(), 0) << "所有定时器触发后链表应为空";
}

#endif /* (MRTK_USING_TIMER == 1) */
