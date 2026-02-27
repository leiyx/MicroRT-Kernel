/**
 * @file mrtk_irq.c
 * @author leiyx
 * @brief 中断管理模块实现
 * @details 实现中断嵌套管理和延迟调度功能
 * @copyright Copyright (c) 2026
 */

#include "mrtk_irq.h"
#include "mrtk_schedule.h"

/* ==============================================================================
 * 全局变量定义
 * ============================================================================== */

/** 中断嵌套计数器（初始化为 0，表示未进入中断） */
volatile mrtk_u8_t g_interrupt_nest = 0;

/* ==============================================================================
 * 中断管理API实现
 * ============================================================================== */

mrtk_void_t mrtk_irq_init(mrtk_void_t)
{
    g_interrupt_nest = 0;
}

mrtk_void_t mrtk_interrupt_enter(mrtk_void_t)
{
    /* 关中断 */
    mrtk_base_t primask = mrtk_hw_interrupt_disable();

    /* 中断嵌套计数器加 1 */
    ++g_interrupt_nest;

    /* 开中断 */
    mrtk_hw_interrupt_enable(primask);
}

mrtk_void_t mrtk_interrupt_leave(mrtk_void_t)
{
    /* 关中断 */
    mrtk_base_t primask = mrtk_hw_interrupt_disable();

    /* 中断嵌套计数器减 1 */
    --g_interrupt_nest;

    /* 若离开最外层中断时调度器未上锁且有调度请求 */
    if (g_interrupt_nest == 0 && g_schedule_lock_nest == 0 && g_need_schedule == 1) {
        /* 任务切换 */
        mrtk_hw_context_switch_interrupt();
        g_need_schedule = MRTK_FALSE;
    }

    /* 开中断 */
    mrtk_hw_interrupt_enable(primask);
}