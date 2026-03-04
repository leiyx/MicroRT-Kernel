/**
 * @file mrtk_system.c
 * @author leiyx
 * @brief 系统统一初始化接口实现
 * @details 提供 mrtk_system_init() 统一入口，完成所有内核组件初始化
 * @note 系统初始化顺序：
 *       1. 中断管理初始化
 *       2. 堆内存初始化（如果启用）
 *       3. 内核对象管理初始化
 *       4. 定时器模块初始化
 *       5. 调度器初始化
 *       6. 空闲任务初始化
 *       7. 定时器守护任务初始化（如果启用软定时器）
 * @copyright Copyright (c) 2026
 */

#include "mrtk.h"
#include "mrtk_irq.h"
#include "mrtk_mem_heap.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"

/**
 * @brief 系统启动标志
 * @details 标识系统是否已经正式点火启动。
 *          在系统未启动前，调度器不执行上下文切换，仅设置延迟调度标志。
 *          此标志在 mrtk_system_start() 中设置为 MRTK_TRUE。
 * @note 默认为 MRTK_FALSE，系统点火后置位
 */
volatile mrtk_u8_t g_mrtk_started = MRTK_FALSE;

mrtk_err_t mrtk_system_init(mrtk_void_t)
{
    mrtk_err_t ret;

    /* 中断管理初始化 */
    mrtk_irq_init();

    /* 初始化堆内存 */
#if (MRTK_USING_MEM_HEAP == 1)
    ret = mrtk_heap_init(g_heap_buffer, g_heap_buffer + MRTK_HEAP_SIZE);
    if (ret != MRTK_EOK) {
        return ret;
    }
#endif

    /* 内核对象管理初始化 */
    _mrtk_obj_system_init();

    /* 定时模块初始化 */
    _mrtk_timer_system_init();

    /* 调度器初始化 */
    ret = mrtk_schedule_init();
    if (ret != MRTK_EOK) {
        return ret;
    }

    /* 空闲任务初始化 */
    ret = mrtk_task_init_idle();
    if (ret != MRTK_EOK) {
        return ret;
    }

#if (MRTK_USING_TIMER_SOFT == 1)
    /* 定时器守护任务初始化 */
    ret = mrtk_task_init_timer_daemon();
    if (ret != MRTK_EOK) {
        return ret;
    }
#endif

    return MRTK_EOK;
}

mrtk_void_t mrtk_system_start(mrtk_void_t)
{
    /* 关中断 */
    mrtk_hw_interrupt_disable();

    /* 设置系统启动标志，允许调度器执行实际的任务切换 */
    g_mrtk_started = MRTK_TRUE;

    /* 手动找出当前就绪队列里优先级最高的任务，赋值给 g_NextTCB */
    mrtk_u8_t         highest_prio          = _mrtk_schedule_get_highest_prio();
    mrtk_list_node_t *highest_tcb_list_node = g_ready_task_list[highest_prio].next;
    g_NextTCB = _mrtk_list_entry(highest_tcb_list_node, mrtk_tcb_t, sched_node);

    /* 在此处初始化并启动 SysTick 硬件定时器 */
    // mrtk_hw_systick_init();

    mrtk_start();
    /* 永不返回 */
}