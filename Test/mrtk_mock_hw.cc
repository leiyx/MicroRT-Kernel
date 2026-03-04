/**
 * @file mrtk_mock_hw.cc
 * @author leiyx
 * @brief 硬件层和内核接口的 Mock 实现
 * @version 0.2
 * @copyright Copyright (c) 2025
 * @note 提供硬件相关函数和内核核心 API 的 Mock 实现，供各测试模块复用
 */

#include "mrtk_mock_hw.hpp"
#include "gmock/gmock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mrtk_errno.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"
#include "mrtk_timer.h"
#include "mrtk_typedef.h"
#include "mrtk_irq.h"

/* ==============================================================================
 * 全局 Mock 对象指针（仅硬件层）
 * ============================================================================== */

static MockCpuPort *g_mock_cpu_port = nullptr;

/* 注意：不再需要 g_mock_kernel 和 g_mock_timer
 * 软件函数直接链接到内核的真实实现
 */

/* ==============================================================================
 * Mock 函数包装器 - CPU Port (C linkage)
 * ============================================================================== */

extern "C" {

/**
 * @brief Mock 函数: 初始化任务栈
 */
mrtk_void_t *mrtk_hw_stack_init(mrtk_void_t *entry, mrtk_void_t *parameter, mrtk_void_t *stack_top,
                                mrtk_void_t *exit)
{
    /* 测试环境下返回一个模拟的栈指针 */
    /* 实际测试中不需要真实的栈初始化，返回 stack_top 即可 */
    return stack_top;
}

/**
 * @brief Mock 函数: 关闭全局中断
 * @return mrtk_base_t 关闭前的中断状态
 */
mrtk_ubase_t mrtk_hw_interrupt_disable(void)
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
void mrtk_hw_interrupt_enable(mrtk_ubase_t level)
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

/**
 * @brief Mock 函数: 启动内核调度器
 * @note 测试环境实现：调用 Mock 对象的对应方法
 */
void mrtk_start(mrtk_void_t)
{
    if (g_mock_cpu_port) {
        g_mock_cpu_port->mrtk_start();
    }
}

/**
 * @brief Mock 函数: 硬件层字符输出接口
 * @param str 要输出的字符串
 * @note 测试环境实现：输出到标准输出（用于调试测试）
 */
void mrtk_hw_output_string(const mrtk_char_t *str)
{
    if (str == nullptr) {
        return;
    }

    /* 测试环境：直接输出到标准输出 */
    while (*str != '\0') {
        putchar((int) *str);
        str++;
    }
}

/* ==============================================================================
 * 注意：以下软件函数不再提供 Mock 包装器
 * ==============================================================================
 *
 * 删除的 Mock 函数（直接链接到内核真实实现）：
 * - mrtk_task_self()      -> 使用 mrtk_task.c 中的实现
 * - mrtk_task_suspend()   -> 使用 mrtk_task.c 中的实现
 * - mrtk_task_resume()    -> 使用 mrtk_task.c 中的实现
 * - _mrtk_obj_init()       -> 使用 mrtk_obj.c 中的实现
 * - _mrtk_obj_delete()     -> 使用 mrtk_obj.c 中的实现
 * - mrtk_tick_get()       -> 使用内核中的实现
 *
 * 只保留硬件层（CPU Port）的 Mock
 */

/* ==============================================================================
 * Mock 管理接口 (C linkage) - 仅 CPU Port
 * ============================================================================== */

/**
 * @brief 设置全局 MockCpuPort 对象
 * @param[in] mock Mock 对象指针
 */
void mrtk_mock_set_cpu_port(void *mock)
{
    g_mock_cpu_port = static_cast<MockCpuPort *>(mock);
}

/**
 * @brief 清除全局 MockCpuPort 对象
 */
void mrtk_mock_clear_cpu_port(void)
{
    g_mock_cpu_port = nullptr;
}

/* ==============================================================================
 * Tick 控制接口实现（用于测试）
 * ============================================================================== */

/**
 * @brief Mock 函数: 获取系统 Tick
 * @return mrtk_u32_t 当前 Tick 值
 * @note 测试环境实现，返回全局 Mock Tick 计数器
 */
mrtk_u32_t mrtk_tick_get(mrtk_void_t)
{
    return g_mrtk_tick;
}

/**
 * @brief 设置系统 Tick 值（用于测试）
 * @param[in] tick 要设置的 Tick 值
 */
void mrtk_mock_set_tick(mrtk_u32_t tick)
{
    g_mrtk_tick = tick;
}

/**
 * @brief 获取系统 Tick 值（用于测试）
 * @return mrtk_u32_t 当前 Tick 值
 */
mrtk_u32_t mrtk_mock_get_tick(mrtk_void_t)
{
    return g_mrtk_tick;
}

/**
 * @brief 推进系统 Tick 值（用于测试）
 * @param[in] ticks 要推进的 Tick 数量
 */
void mrtk_mock_advance_tick(mrtk_u32_t ticks)
{
    g_mrtk_tick += ticks;
}

} /* extern "C" */
