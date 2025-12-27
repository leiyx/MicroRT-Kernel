/**
 * @file mrtk_test_irq.cc
 * @author leiyx
 * @brief 中断管理模块 (mrtk_irq.c) 的单元测试
 * @version 0.1
 * @copyright Copyright (c) 2025
 */

#include "gmock/gmock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
#include "mrtk_irq.h"
#include "mrtk_typedef.h"
}

/* --- Mock 类定义 --- */

/**
 * @class MockCpuPort
 * @brief CPU 端口层函数的 Mock 类
 * @note 用于模拟硬件相关的临界区和上下文切换操作
 */
class MockCpuPort {
  public:
    MOCK_METHOD((mrtk_base_t), mrtk_hw_interrupt_disable, (), ());
    MOCK_METHOD((void), mrtk_hw_interrupt_enable, (mrtk_base_t level), ());
    MOCK_METHOD((void), mrtk_hw_context_switch_interrupt, (), ());
};

/* --- 全局 Mock 对象 --- */
static MockCpuPort *g_mock_cpu_port = nullptr;

/* --- Mock 函数包装器 (C linkage) --- */
extern "C" {

/**
 * @brief Mock 函数: 关闭全局中断
 * @return mrtk_base_t 关闭前的中断状态
 */
mrtk_base_t mrtk_hw_interrupt_disable(void)
{
    if (g_mock_cpu_port) {
        return g_mock_cpu_port->mrtk_hw_interrupt_disable();
    }
    return 0;
}

/**
 * @brief Mock 函数: 恢复全局中断
 * @param[in] level 之前保存的中断状态
 */
void mrtk_hw_interrupt_enable(mrtk_base_t level)
{
    if (g_mock_cpu_port) {
        g_mock_cpu_port->mrtk_hw_interrupt_enable(level);
    }
}

/**
 * @brief Mock 函数: 在中断中触发上下文切换
 */
void mrtk_hw_context_switch_interrupt(void)
{
    if (g_mock_cpu_port) {
        g_mock_cpu_port->mrtk_hw_context_switch_interrupt();
    }
}

} /* extern "C" */

/* --- 测试固件 (Test Fixture) --- */

/**
 * @class MrtkIrqTest
 * @brief 中断管理测试固件
 * @details 负责:
 *          - 测试前的环境初始化 (设置 Mock 对象)
 *          - 测试后的环境清理 (重置全局变量)
 */
class MrtkIrqTest : public ::testing::Test {
  protected:
    /**
     * @brief 测试前初始化
     */
    void SetUp() override
    {
        /* 创建 Mock 对象 */
        g_mock_cpu_port = &mock_cpu_port;

        /* 重置全局变量 */
        g_interrupt_nest = 0;
        g_need_schedule  = 0;
    }

    /**
     * @brief 测试后清理
     */
    void TearDown() override
    {
        /* 清理 Mock 对象 */
        g_mock_cpu_port = nullptr;
    }

    /* Mock 对象实例 */
    MockCpuPort mock_cpu_port;
};

/**
 * @test 测试多次进入\离开中断时的嵌套计数
 * @details 模拟深层中断嵌套场景
 */
TEST_F(MrtkIrqTest, Interrupt_MultipleNesting)
{
    const mrtk_u8_t enter_times = 5;
    const mrtk_u8_t leave_times = 3;
    /* 设置期望: 每次进入/离开临界区都会调用 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .Times(enter_times + leave_times)
        .WillRepeatedly(::testing::Return(0));

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(::testing::_))
        .Times(enter_times + leave_times);

    for (mrtk_u8_t i = 0; i < enter_times; ++i) {
        mrtk_interrupt_enter();
    }
    EXPECT_EQ(g_interrupt_nest, enter_times);
    for (mrtk_u8_t i = 0; i < leave_times; ++i) {
        mrtk_interrupt_leave();
    }
    EXPECT_EQ(g_interrupt_nest, enter_times - leave_times);
}

/**
 * @test 测试 PRIMASK 值的传递
 * @details 验证不同的 PRIMASK 值能被正确保存和恢复
 */
TEST_F(MrtkIrqTest, Interrupt_PrimaryskValueIntegrity)
{
    const mrtk_base_t k_test_values[] = {0x00, 0x01, 0xFF, 0x80};

    for (const auto &primask : k_test_values) {
        testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);

        EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
            .WillOnce(::testing::Return(primask));

        EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(primask)).Times(1);

        mrtk_interrupt_enter();
    }
}

/**
 * @test 测试离开最外层中断时不触发调度
 * @details 验证: 当 g_need_schedule == 0 时, 不会调用 mrtk_hw_context_switch_interrupt
 */
TEST_F(MrtkIrqTest, InterruptLeave_NoScheduleNeeded)
{
    /* 设置期望: 进入临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .WillOnce(::testing::Return(0))
        .WillOnce(::testing::Return(0));

    /* 设置期望: 离开临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(0)).Times(2);

    /* 设置期望: 不应该调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* 进入并离开中断 */
    mrtk_interrupt_enter();
    EXPECT_EQ(g_interrupt_nest, 1);

    /* 离开中断 (g_need_schedule == 0) */
    mrtk_interrupt_leave();
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, 0);
}

/**
 * @test 测试离开最外层中断时触发调度
 * @details 验证:
 *          - 当 g_interrupt_nest == 0 && g_need_schedule == 1 时
 *          - 调用 mrtk_hw_context_switch_interrupt
 *          - 复位 g_need_schedule 为 0
 */
TEST_F(MrtkIrqTest, InterruptLeave_TriggerSchedule)
{
    /* 设置期望: 进入临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .WillOnce(::testing::Return(0))
        .WillOnce(::testing::Return(0));

    /* 设置期望: 离开临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(0)).Times(2);

    /* 设置期望: 应该调用上下文切换 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);

    /* 进入中断并设置调度标识 */
    mrtk_interrupt_enter();
    g_need_schedule = 1;

    /* 离开中断 (应该触发调度) */
    mrtk_interrupt_leave();

    /* 验证状态 */
    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, 0); /* 应该被复位 */
}

/**
 * @test 测试中断嵌套时不触发调度
 * @details 验证:
 *          - 即使 g_need_schedule == 1
 *          - 如果 g_interrupt_nest > 0, 不会触发调度
 *          - 只有在最外层中断离开时才会触发
 */
TEST_F(MrtkIrqTest, InterruptLeave_NestedInterrupt_NoSchedule)
{
    /* 设置期望: 进入临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .Times(3)
        .WillRepeatedly(::testing::Return(0));

    /* 设置期望: 离开临界区 */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(0)).Times(3);

    /* 设置期望: 不应该调用上下文切换 (因为还在嵌套中) */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* 进入 2 层中断 */
    mrtk_interrupt_enter();
    mrtk_interrupt_enter();
    EXPECT_EQ(g_interrupt_nest, 2);

    /* 设置调度标识 */
    g_need_schedule = 1;

    /* 离开内层中断 (不应该触发调度) */
    mrtk_interrupt_leave();
    EXPECT_EQ(g_interrupt_nest, 1);
    EXPECT_EQ(g_need_schedule, 1); /* 标识仍然保持 */
}

/**
 * @test 测试完整的 ISR 调用流程
 * @details 模拟真实的中断服务程序 (ISR) 调用场景
 */
TEST_F(MrtkIrqTest, ISR_CompleteFlow)
{
    const mrtk_base_t k_primask_value = 0x00;

    /* 第一次 ISR */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .Times(2)
        .WillRepeatedly(::testing::Return(k_primask_value));

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(k_primask_value)).Times(2);

    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    /* 模拟第一次 ISR: enter -> 执行 -> leave */
    mrtk_interrupt_enter();
    /* ... 用户 ISR 代码 ... */
    mrtk_interrupt_leave();

    EXPECT_EQ(g_interrupt_nest, 0);

    /* 第二次 ISR (带嵌套) */
    testing::Mock::VerifyAndClearExpectations(&mock_cpu_port);

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_disable())
        .Times(4)
        .WillRepeatedly(::testing::Return(k_primask_value));

    EXPECT_CALL(mock_cpu_port, mrtk_hw_interrupt_enable(k_primask_value)).Times(4);

    /* 外层 ISR */
    mrtk_interrupt_enter();

    /* 内层 ISR (嵌套) */
    mrtk_interrupt_enter();
    /* ... 内层 ISR 代码 ... */
    mrtk_interrupt_leave();

    /* 外层 ISR 结束 (触发调度) */
    g_need_schedule = 1;
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(1);

    mrtk_interrupt_leave();

    EXPECT_EQ(g_interrupt_nest, 0);
    EXPECT_EQ(g_need_schedule, 0);
}
