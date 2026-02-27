/**
 * @file mrtk_mock_hw.hpp
 * @author leiyx
 * @brief 硬件层和内核接口的 Mock 类声明
 * @version 0.2
 * @copyright Copyright (c) 2025
 * @note 提供硬件相关函数和内核核心 API 的 Mock 类声明，供各测试模块复用
 */

#ifndef MRTK_MOCK_HW_HPP
#define MRTK_MOCK_HW_HPP

#include <gmock/gmock.h>

/* MRTK 头文件（已有自己的 extern "C"） */
#include "mrtk_typedef.h"
#include "mrtk_errno.h"
#include "mrtk_task.h"
#include "mrtk_obj.h"

/**
 * @class MockCpuPort
 * @brief CPU 端口层函数的 Mock 类
 * @note 用于模拟硬件相关的临界区和上下文切换操作
 *
 * 使用示例:
 * @code
 * class MyTest : public ::testing::Test {
 *   protected:
 *     MockCpuPort mock_cpu_port;
 *
 *     void SetUp() override {
 *       mrtk_mock_set_cpu_port(&mock_cpu_port);
 *     }
 *
 *     void TearDown() override {
 *       mrtk_mock_clear_cpu_port();
 *     }
 * };
 * @endcode
 */
class MockCpuPort {
  public:
    MOCK_METHOD((mrtk_base_t), mrtk_hw_interrupt_disable, (), ());
    MOCK_METHOD((void), mrtk_hw_interrupt_enable, (mrtk_base_t level), ());
    MOCK_METHOD((void), mrtk_hw_context_switch_interrupt, (), ());
    MOCK_METHOD((void), mrtk_start, (), ());
};

/* 注意：MockKernel 和 MockTimer 已被移除
 * 原因：软件流程不应被 mock，只有硬件层（CPU Port）需要 mock
 * 任务管理、对象管理、定时器等软件模块应使用真实实现
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ========== CPU Port Mock 接口 ========== */

/**
 * @brief 设置全局 MockCpuPort 对象
 * @param[in] mock Mock 对象指针
 */
void mrtk_mock_set_cpu_port(void *mock);

/**
 * @brief 清除全局 MockCpuPort 对象
 */
void mrtk_mock_clear_cpu_port(void);


/* ========== Tick 控制接口 ========== */

/**
 * @brief 设置系统 Tick 值（用于测试）
 * @param[in] tick 要设置的 Tick 值
 * @note 仅在测试环境中使用，应用代码不应调用
 */
void mrtk_mock_set_tick(mrtk_u32_t tick);

/**
 * @brief 获取系统 Tick 值（用于测试）
 * @return 当前 Tick 值
 * @note 仅在测试环境中使用，应用代码不应调用
 */
mrtk_u32_t mrtk_mock_get_tick(mrtk_void_t);

/**
 * @brief 推进系统 Tick 值（用于测试）
 * @param[in] ticks 要推进的 Tick 数量
 * @note 仅在测试环境中使用，应用代码不应调用
 */
void mrtk_mock_advance_tick(mrtk_u32_t ticks);

/* ========== 注意：软件函数不提供 Mock 接口 ========== */
/*
 * 以下软件函数不再提供 Mock：
 * - mrtk_task_self, mrtk_task_suspend, mrtk_task_resume (任务管理)
 * - mrtk_obj_init, mrtk_obj_delete (对象管理)
 *
 * 这些函数直接使用内核的真实实现，只在硬件层提供 Mock
 *
 * 注意：mrtk_tick_get() 在测试环境中使用 Mock 实现（通过 mrtk_mock_*_tick 函数控制）
 */

#ifdef __cplusplus
}
#endif

#endif /* MRTK_MOCK_HW_HPP */
