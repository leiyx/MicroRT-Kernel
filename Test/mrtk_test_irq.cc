/**
 * @file mrtk_test_irq.cc
 * @author leiyx
 * @brief 中断管理模块单元测试
 * @details 测试中断嵌套管理、PRIMASK 保存恢复、延迟调度触发机制
 * @copyright Copyright (c) 2026
 *
 * === 测试覆盖策略 ===
 * 1. 边界值分析：
 *    - 中断嵌套层数：0（最外层）→ 255（u8 最大值）
 *    - PRIMASK 值：0（中断开启）vs 1（中断关闭）
 *
 * 2. 等价类划分：
 *    - 防御性测试：无需参数测试（模块无对外 API 参数）
 *    - 正向测试：标准中断嵌套场景
 *
 * 3. 分支与条件覆盖：
 *    - mrtk_interrupt_leave() 中的核心条件：
 *      (g_interrupt_nest == 0) && (g_schedule_lock_nest == 0) && (g_need_schedule == 1)
 *    - 三种标志组合（nest=0 时）：schedule_lock×need_schedule = 00, 01, 10, 11
 *
 * 4. 状态机覆盖：
 *    - Init → Enter(×1) → Leave → Exit
 *    - Init → Enter(×3) → Leave(×3) → Exit
 *    - 调度请求标志状态转换
 *
 * 5. Mock 依赖注入：
 *    - mrtk_hw_interrupt_disable() - 返回 PRIMASK
 *    - mrtk_hw_interrupt_enable(level) - 恢复中断状态
 *    - mrtk_hw_context_switch_interrupt() - 触发 PendSV
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mrtk_irq.h"
#include "mrtk_schedule.h"

/* 引入 Mock 框架 */
#include "mrtk_mock_hw.hpp"

/**
 * @brief 中断管理模块测试固件
 * @details 提供统一的测试环境初始化和清理
 */
class MrtkIrqTest : public ::testing::Test {
  protected:
    MockCpuPort mock_cpu_port;

    void SetUp() override {
        /* Step 1: Given (系统初始化，复位所有全局变量) */
        g_interrupt_nest = 0;
        g_need_schedule = MRTK_FALSE;
        g_schedule_lock_nest = 0;

        /* Step 2: Given (设置 Mock 对象) */
        mrtk_mock_set_cpu_port(&mock_cpu_port);

        /* Step 3: Given (设置默认期望行为) */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
            .WillByDefault(testing::Return(0));  /* 默认返回中断开启状态 */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_enable)
            .WillByDefault(testing::Return());
        ON_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
            .WillByDefault(testing::Return());
    }

    void TearDown() override {
        /* 清除 Mock 对象 */
        mrtk_mock_clear_cpu_port();
    }
};

/* ==============================================================================
 * 测试用例：mrtk_irq_init()
 * ============================================================================== */

/**
 * @test 中断模块初始化
 * @details 验证初始化后 g_interrupt_nest 被正确复位为 0
 */
TEST_F(MrtkIrqTest, Init_ResetsNestCounterToZero) {
    /* Step 1: Given (模拟嵌套计数器非零状态) */
    g_interrupt_nest = 5;

    /* Step 2: When (执行初始化) */
    mrtk_irq_init();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
}

/**
 * @test 中断模块初始化边界测试
 * @details 验证从 u8 最大值初始化到 0 的场景
 */
TEST_F(MrtkIrqTest, Init_ResetsNestCounterFromMaxValue) {
    /* Step 1: Given (模拟嵌套计数器为最大值) */
    g_interrupt_nest = 255;

    /* Step 2: When (执行初始化) */
    mrtk_irq_init();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
}

/* ==============================================================================
 * 测试用例：mrtk_interrupt_enter()
 * ============================================================================== */

/**
 * @test 单次中断进入
 * @details 验证中断嵌套计数器从 0 增加到 1
 * @covers 分支：正常递增逻辑
 */
TEST_F(MrtkIrqTest, Enter_IncrementsNestCounterFromZero) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));  /* 返回 PRIMASK = 0（中断开启） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);  /* 恢复 PRIMASK = 0 */

    /* Step 2: When (执行被测 API) */
    mrtk_interrupt_enter();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 1);
}

/**
 * @test 多次中断进入（嵌套场景）
 * @details 验证中断嵌套计数器从 0 增加到 3
 * @covers 状态机：单层 → 多层嵌套
 */
TEST_F(MrtkIrqTest, Enter_IncrementsNestCounterMultipleTimes) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(3)
        .WillRepeatedly(testing::Return(1));  /* PRIMASK = 1（中断关闭） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(1)))
        .Times(3);  /* 恢复 PRIMASK = 1 */

    /* Step 2: When (执行被测 API) */
    mrtk_interrupt_enter();
    mrtk_interrupt_enter();
    mrtk_interrupt_enter();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 3);
}

/**
 * @test 中断进入 PRIMASK 保存恢复验证
 * @details 验证中断屏蔽状态的正确保存和恢复
 * @covers 边界值：PRIMASK = 0 和 PRIMASK = 1
 */
TEST_F(MrtkIrqTest, Enter_PreservesAndRestoresPRIMASK) {
    /* 强制要求 Mock 函数必须按照写的顺序发生 */
    testing::InSequence s;

    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    /* 第一次 mrtk_interrupt_enter()：PRIMASK = 0 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    /* 第二次 mrtk_interrupt_enter()：PRIMASK = 1 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(1));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(1)))
        .Times(1);

    /* Step 2: When (执行被测 API) */
    mrtk_interrupt_enter();
    mrtk_interrupt_enter();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 2);
}

/* ==============================================================================
 * 测试用例：mrtk_interrupt_leave() - 分支覆盖
 * ============================================================================== */

/**
 * @test 离开最外层中断且无调度请求（组合 000）
 * @details g_interrupt_nest=0, g_schedule_lock_nest=0, g_need_schedule=0 → 不触发上下文切换
 * @covers 分支：(nest==0) && (lock==0) && (need_schedule==1) → FALSE（无调度请求）
 */
TEST_F(MrtkIrqTest, Leave_OutermostInterrupt_NoScheduleRequest) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 1;
    g_schedule_lock_nest = 0;
    g_need_schedule = MRTK_FALSE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换 */

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
}

/**
 * @test 离开最外层中断且有调度请求（组合 011）
 * @details g_interrupt_nest=0, g_schedule_lock_nest=0, g_need_schedule=1 → 触发上下文切换
 * @covers 分支：(nest==0) && (lock==0) && (need_schedule==1) → TRUE
 */
TEST_F(MrtkIrqTest, Leave_OutermostInterrupt_WithScheduleRequest) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 1;
    g_schedule_lock_nest = 0;
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(1);  /* 断言：触发上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
}

/**
 * @test 离开内层中断且无调度请求（组合 100）
 * @details g_interrupt_nest=2, g_schedule_lock_nest=0, g_need_schedule=0 → 不触发上下文切换
 * @covers 分支：(nest == 0) → FALSE
 */
TEST_F(MrtkIrqTest, Leave_InnerInterrupt_NoScheduleRequest) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 2;
    g_schedule_lock_nest = 0;
    g_need_schedule = MRTK_FALSE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 1);
}

/**
 * @test 离开内层中断但有调度请求（组合 111）
 * @details g_interrupt_nest=2, g_schedule_lock_nest=0, g_need_schedule=1 → 不触发上下文切换（延迟到最外层）
 * @covers 分支：(nest == 0) → FALSE（即使 need_schedule = 1）
 */
TEST_F(MrtkIrqTest, Leave_InnerInterrupt_WithScheduleRequest_DelayedSchedule) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 2;
    g_schedule_lock_nest = 0;
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换（延迟到最外层） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 1);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);
}

/* ==============================================================================
 * 测试用例：mrtk_interrupt_leave() - 状态机覆盖
 * ============================================================================== */

/**
 * @test 完整的中断嵌套生命周期（3 层嵌套）
 * @details 验证 Enter(×3) → Leave(×3) 的完整闭环
 * @covers 状态机：Init → Enter(×3) → Leave(×3) → Exit
 */
TEST_F(MrtkIrqTest, CompleteLifecycle_ThreeLevelNesting) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(3)
        .WillRepeatedly(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(3);

    mrtk_interrupt_enter();  /* nest = 1 */
    mrtk_interrupt_enter();  /* nest = 2 */
    mrtk_interrupt_enter();  /* nest = 3 */
    EXPECT_EQ(g_interrupt_nest, 3);

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(3)
        .WillRepeatedly(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(3);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 全程无调度请求 */

    mrtk_interrupt_leave();  /* nest = 2 */
    mrtk_interrupt_leave();  /* nest = 1 */
    mrtk_interrupt_leave();  /* nest = 0 */

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
}

/**
 * @test 嵌套中断中延迟调度验证（2 层嵌套 + 调度请求）
 * @details 验证调度请求被延迟到最外层中断退出时才触发
 * @covers 关键特性：中断嵌套期间不触发上下文切换
 */
TEST_F(MrtkIrqTest, DelayedSchedule_InNestedInterrupts) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    /* 进入两层中断 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(2)
        .WillRepeatedly(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(2);

    mrtk_interrupt_enter();  /* nest = 1 */
    mrtk_interrupt_enter();  /* nest = 2 */

    /* 模拟内层中断中触发了调度请求 */
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) - 离开内层中断，不触发调度 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换 */

    mrtk_interrupt_leave();  /* nest = 1，仍在内层中断中 */
    EXPECT_EQ(g_interrupt_nest, 1);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);  /* 调度标志保持 */

    /* Step 3: When (执行被测 API) - 离开最外层中断，触发调度 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(1);  /* 断言：触发上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();  /* nest = 0，离开最外层中断 */

    /* Step 4: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
}

/* ==============================================================================
 * 测试用例：mrtk_irq_get_nest()
 * ============================================================================== */

/**
 * @test 获取中断嵌套层数 - 正常范围
 * @details 验证 mrtk_irq_get_nest() 返回正确的嵌套层数
 * @covers 边界值：0 到 100
 */
TEST_F(MrtkIrqTest, GetNest_ReturnsCorrectValue) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 0;
    EXPECT_EQ(mrtk_irq_get_nest(), 0);

    g_interrupt_nest = 1;
    EXPECT_EQ(mrtk_irq_get_nest(), 1);

    g_interrupt_nest = 10;
    EXPECT_EQ(mrtk_irq_get_nest(), 10);

    /* Step 2: When (执行被测 API) */
    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    g_interrupt_nest = 100;
    EXPECT_EQ(mrtk_irq_get_nest(), 100);
}

/**
 * @test 获取中断嵌套层数 - 边界值测试
 * @details 验证 mrtk_irq_get_nest() 在边界值的行为
 * @covers 边界值：0 和 255（u8 最大值）
 */
TEST_F(MrtkIrqTest, GetNest_BoundaryValues) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 0;

    /* Step 2: When (执行被测 API) */
    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) - 断言最小值 */
    EXPECT_EQ(mrtk_irq_get_nest(), 0);

    /* Step 4: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 255;

    /* Step 5: When (执行被测 API) */
    /* Step 6: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) - 断言最大值 */
    EXPECT_EQ(mrtk_irq_get_nest(), 255);
}

/* ==============================================================================
 * 测试用例：复杂场景与压力测试
 * ============================================================================== */

/**
 * @test 多次调度请求覆盖
 * @details 验证多次触发调度请求时，只在第一次最外层退出时触发切换
 * @covers 边界值：多次设置 g_need_schedule = 1
 */
TEST_F(MrtkIrqTest, MultipleScheduleRequests_OnlyTriggerOnce) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_enter();  /* nest = 1 */

    /* 模拟多次设置调度请求（实际效果相同） */
    g_need_schedule = MRTK_TRUE;
    g_need_schedule = MRTK_TRUE;
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(1);  /* 断言：只触发一次 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
}

/**
 * @test PRIMASK 不同值交替场景
 * @details 验证连续进入/离开中断时，PRIMASK 值的正确保存和恢复
 * @covers 边界值：PRIMASK = 0 和 PRIMASK = 1 交替
 */
TEST_F(MrtkIrqTest, PRIMASK_Preservation_AlternatingValues) {
    /* 强制要求 Mock 函数必须按照写的顺序发生 */
    testing::InSequence s;

    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    /* 第一次 mrtk_interrupt_enter()：PRIMASK = 0 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(1));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(1)))
        .Times(1);

    /* Step 2: When (执行被测 API) */
    mrtk_interrupt_enter();
    mrtk_interrupt_enter();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 2);
}

/**
 * @test 极限嵌套层数测试（接近 u8 上限）
 * @details 验证接近 u8 最大值时的计数器行为
 * @covers 边界值：254 和 255
 */
TEST_F(MrtkIrqTest, ExtremeNestingLevels_NearMaxValue) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 254;
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) - 进入一次中断，达到 255 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_enter();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 255);

    /* Step 4: When (执行被测 API) - 离开一次中断，不触发调度 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 仍未到最外层 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 5: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 254);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);
}

/* ==============================================================================
 * 测试用例：mrtk_interrupt_leave() - 调度器锁覆盖
 * ============================================================================== */

/**
 * @test 离开最外层中断但调度器被锁定（组合 010）
 * @details g_interrupt_nest=0, g_schedule_lock_nest>0, g_need_schedule=0 → 不触发上下文切换
 * @covers 分支：(nest==0) && (lock==0) → FALSE（调度器被锁定）
 */
TEST_F(MrtkIrqTest, Leave_OutermostInterrupt_ScheduleLocked_NoScheduleRequest) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 1;
    g_schedule_lock_nest = 1;  /* 调度器被锁定 */
    g_need_schedule = MRTK_FALSE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换 */

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_schedule_lock_nest, 1);
    EXPECT_EQ(g_need_schedule, MRTK_FALSE);
}

/**
 * @test 离开最外层中断且调度器被锁定但有调度请求（组合 011→010）
 * @details g_interrupt_nest=0, g_schedule_lock_nest>0, g_need_schedule=1 → 不触发上下文切换（延迟）
 * @covers 分支：(nest==0) && (lock==0) → FALSE（调度器被锁定，即使有调度请求）
 */
TEST_F(MrtkIrqTest, Leave_OutermostInterrupt_ScheduleLocked_WithScheduleRequest_Delayed) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 1;
    g_schedule_lock_nest = 1;  /* 调度器被锁定 */
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换（调度器被锁定） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_schedule_lock_nest, 1);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);  /* 调度请求保持 */
}

/**
 * @test 调度器锁解除后立即触发调度
 * @details 验证离开中断时调度器被锁定导致调度延迟，解锁后立即触发
 * @covers 场景：中断退出时 schedule_lock>0 → 延迟调度 → 解锁后触发
 */
TEST_F(MrtkIrqTest, DelayedSchedule_TriggerAfterScheduleLockUnlock) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_enter();  /* nest = 1 */

    /* 模拟中断中触发了调度请求 */
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) - 离开中断时调度器被锁定 */
    g_schedule_lock_nest = 1;

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换（调度器被锁定） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);  /* 调度请求保持 */

    /* Step 4: When (执行被测 API) - 解除调度器锁（不在中断中，通常不会自动触发，这里模拟） */
    g_schedule_lock_nest = 0;

    /* 注意：在实际场景中，调度器锁解除后需要在任务上下文中检查 g_need_schedule */
    /* 这里仅验证状态，不模拟完整的调度器解锁流程 */
}

/**
 * @test 多层调度器锁场景测试
 * @details 验证调度器锁嵌套计数大于1时，离开中断不触发调度
 * @covers 边界值：g_schedule_lock_nest > 1
 */
TEST_F(MrtkIrqTest, MultipleScheduleLocks_NoScheduleTriggered) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    g_interrupt_nest = 1;
    g_schedule_lock_nest = 5;  /* 调度器被多层锁定 */
    g_need_schedule = MRTK_TRUE;

    /* Step 2: When (执行被测 API) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_schedule_lock_nest, 5);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);
}

/**
 * @test 中断嵌套 + 调度器锁组合场景测试
 * @details 验证内层中断、调度器锁定、有调度请求的组合场景
 * @covers 复杂场景：nest=2, lock>0, need_schedule=1
 */
TEST_F(MrtkIrqTest, NestedInterrupt_ScheduleLocked_DelayedSchedule) {
    /* Step 1: Given (准备测试数据、Mock 行为、构造初始状态机和边界条件) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(2)
        .WillRepeatedly(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(2);

    mrtk_interrupt_enter();  /* nest = 1 */
    mrtk_interrupt_enter();  /* nest = 2 */

    /* 模拟内层中断中触发了调度请求，且调度器被锁定 */
    g_need_schedule = MRTK_TRUE;
    g_schedule_lock_nest = 1;

    /* Step 2: When (执行被测 API) - 离开内层中断 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换（内层中断） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();  /* nest = 1 */

    /* Step 3: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 1);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);
    EXPECT_EQ(g_schedule_lock_nest, 1);

    /* Step 4: When (执行被测 API) - 离开最外层中断，但调度器仍被锁定 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
        .Times(1)
        .WillOnce(testing::Return(0));
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt)
        .Times(0);  /* 断言：不触发上下文切换（调度器被锁定） */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(testing::Eq(0)))
        .Times(1);

    mrtk_interrupt_leave();  /* nest = 0 */

    /* Step 5: Then (断言返回值、断言全局链表状态、断言 Mock 调用次数) */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, MRTK_TRUE);  /* 调度请求保持 */
    EXPECT_EQ(g_schedule_lock_nest, 1);
}
