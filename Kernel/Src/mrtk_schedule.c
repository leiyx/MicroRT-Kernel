/**
 * @file mrtk_schedule.c
 * @author leiyx
 * @brief 调度器模块实现
 * @details 实现基于优先级的抢占式调度器，O(1) 调度算法
 * @copyright Copyright (c) 2026
 */

#include "mrtk_schedule.h"
#include "cpu_port.h"
#include "mrtk_config_internal.h"
#include "mrtk_errno.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_obj.h"
#include "mrtk_task.h"
#include "mrtk_typedef.h"

/* ==============================================================================
 * 全局变量定义
 * ============================================================================== */

/** 已终止任务的链表头 */
mrtk_list_t g_defunct_task_list = {&g_defunct_task_list, &g_defunct_task_list};

/** 就绪任务队列数组（按优先级索引） */
mrtk_list_node_t g_ready_task_list[MRTK_MAX_PRIO_LEVEL_NUM];

/** 就绪任务优先级位图 */
volatile mrtk_u32_t g_ready_prio_bitmap;

/** 当前运行任务的 TCB 指针 */
mrtk_tcb_t *volatile g_CurrentTCB;

/** 下一个要运行的任务的 TCB 指针 */
mrtk_tcb_t *volatile g_NextTCB;

/** 调度锁嵌套计数器 */
volatile mrtk_u32_t g_schedule_lock_nest = 0;

/** 延迟调度标志 */
volatile mrtk_u8_t g_need_schedule = MRTK_FALSE;

/* ==============================================================================
 * 优先级查找表定义 (0-255)
 * ============================================================================== */

/**
 * @brief 统一的位图查找表
 * @details 根据优先级配置 MRTK_PRIO_HIGHER_IS_LOWER_VALUE 决定查找方式：
 *          - MRTK_PRIO_HIGHER_IS_LOWER_VALUE = 1：索引为 value，值为最低位 '1' 的位置
 *            例如：输入 0x10 (二进制 0001 0000)，返回 4
 *          - MRTK_PRIO_HIGHER_IS_LOWER_VALUE = 0：索引为 value，值为最高位 '1' 的位置
 *            例如：输入 0x18 (二进制 0001 1000)，返回 4
 * @note 用于不支持硬件 CLZ 的 MCU (如 Cortex-M0) 实现 O(1) 查找
 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
static const mrtk_u8_t __bit_bitmap[] = {
    /* 00 */ 0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 10 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 20 */ 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 30 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 40 */ 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 50 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 60 */ 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 70 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 80 */ 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* 90 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* A0 */ 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* B0 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* C0 */ 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* D0 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* E0 */ 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
    /* F0 */ 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
#else
static const mrtk_u8_t __bit_bitmap[] = {
    /* 00 */ 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 10 */ 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    /* 20 */ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    /* 30 */ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    /* 40 */ 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 50 */ 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 60 */ 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 70 */ 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    /* 80 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* 90 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* A0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* B0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* C0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* D0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* E0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* F0 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
#endif

/* ==============================================================================
 * 调度器内部函数
 * ============================================================================== */

mrtk_u8_t _mrtk_schedule_get_highest_prio(mrtk_void_t)
{
    mrtk_u32_t bitmap = g_ready_prio_bitmap;

    if (bitmap == 0) {
        /* 系统异常：不应该没有任何就绪任务，返回空闲任务优先级 */
        return MRTK_IDLE_PRIORITY;
    }

#if (MRTK_CPU_HAS_CLZ == 1)
    /* 硬件指令实现（适用于 Cortex-M3/M4/M7） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 数值越小优先级越高：使用 CTZ 查找最低置位位 */
    return (mrtk_u8_t) (__builtin_ctz(bitmap));
#else
    /* 数值越大优先级越高：使用 CLZ 查找最高置位位 */
    return (mrtk_u8_t) (31 - __builtin_clz(bitmap));
#endif
#else
    /* 软件查找表实现（适用于 Cortex-M0/M0+） */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
    /* 数值越小优先级越高：从低位到高位查找最低置位位 */
    if (bitmap & 0xFF) {
        return __bit_bitmap[bitmap & 0xFF];
    } else if (bitmap & 0xFF00) {
        return __bit_bitmap[(bitmap >> 8) & 0xFF] + 8;
    } else if (bitmap & 0xFF0000) {
        return __bit_bitmap[(bitmap >> 16) & 0xFF] + 16;
    } else {
        return __bit_bitmap[(bitmap >> 24) & 0xFF] + 24;
    }
#else
    /* 数值越大优先级越高：从高位到低位查找最高置位位 */
    if (bitmap & 0xFF000000) {
        return __bit_bitmap[(bitmap >> 24) & 0xFF] + 24;
    } else if (bitmap & 0xFF0000) {
        return __bit_bitmap[(bitmap >> 16) & 0xFF] + 16;
    } else if (bitmap & 0xFF00) {
        return __bit_bitmap[(bitmap >> 8) & 0xFF] + 8;
    } else {
        return __bit_bitmap[bitmap & 0xFF];
    }
#endif
#endif
}

/* ==============================================================================
 * 调度器公共 API
 * ============================================================================== */

mrtk_err_t mrtk_schedule_init(void)
{
    /* 初始化就绪任务队列数组 */
    for (mrtk_u32_t i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
        _mrtk_list_init(&g_ready_task_list[i]);
    }

    _mrtk_list_init(&g_defunct_task_list);

    /* 初始化优先级位图 */
    g_ready_prio_bitmap = 0;

    /*  初始化调度锁嵌套计数 */
    g_schedule_lock_nest = 0;

    /* 初始化延迟调度标志 */
    g_need_schedule = MRTK_FALSE;

    /* 初始化任务切换所需tcb指针 */
    g_CurrentTCB = MRTK_NULL;
    g_NextTCB    = MRTK_NULL;

    return MRTK_EOK;
}

mrtk_void_t mrtk_schedule(mrtk_void_t)
{
    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 0. 系统未启动时不执行实际调度，仅设置延迟调度标志 */
    if (g_mrtk_started == MRTK_FALSE) {
        g_need_schedule = MRTK_TRUE;
        mrtk_hw_interrupt_enable(level);
        return;
    }

    /* 1. 若在中断上下文中，设置延迟调度标志，退出最外层中断时触发调度 */
    /* 2. 若调度器已上锁，设置延迟调度标志，调度器解锁时触发调度 */
    if (g_interrupt_nest > 0 || g_schedule_lock_nest > 0) {
        g_need_schedule = MRTK_TRUE;
        mrtk_hw_interrupt_enable(level);
        return;
    }

    /* 3. 若任务上下文且调度器未上锁 */
    g_need_schedule = MRTK_FALSE;

    /* 查找最高优先级任务 */
    mrtk_u8_t         highest_prio          = _mrtk_schedule_get_highest_prio();
    mrtk_list_node_t *highest_tcb_list_node = g_ready_task_list[highest_prio].next;
    g_NextTCB = _mrtk_list_entry(highest_tcb_list_node, mrtk_tcb_t, sched_node);

    /* 如果最高优先级任务就是当前任务，无需切换 */
    if (g_NextTCB != g_CurrentTCB) {
        mrtk_hw_context_switch_interrupt();
    }

    mrtk_hw_interrupt_enable(level);
}

mrtk_void_t _mrtk_schedule_insert_task(mrtk_task_t *task)
{
    /* 插入到就绪队列队尾，保证同级任务按时间片轮转 */
    _mrtk_list_insert_before(&g_ready_task_list[task->priority], &task->sched_node);

    /* 更新位图：将对应优先级位置 1 */
    MRTK_SET_BIT(g_ready_prio_bitmap, task->priority);
}

mrtk_void_t _mrtk_schedule_remove_task(mrtk_task_t *task)
{
    /* 从就绪队列中移除任务 */
    _mrtk_list_remove(&task->sched_node);

    /* 只有当该优先级下没有任务时，才清除位图标志 */
    if (_mrtk_list_is_empty(&g_ready_task_list[task->priority])) {
        MRTK_CLR_BIT(g_ready_prio_bitmap, task->priority);
    }
}

mrtk_void_t mrtk_schedule_lock(mrtk_void_t)
{
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    ++g_schedule_lock_nest;

    mrtk_hw_interrupt_enable(level);
}

mrtk_void_t mrtk_schedule_unlock(mrtk_void_t)
{
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    --g_schedule_lock_nest;

    if (g_interrupt_nest > 0 || g_schedule_lock_nest > 0) {
        mrtk_hw_interrupt_enable(level);
        return;
    }

    /* 调度锁计数器归零，真正解锁 */
    g_schedule_lock_nest      = 0;
    mrtk_bool_t need_schedule = g_need_schedule;

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一触发调度 */
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
}

mrtk_void_t _mrtk_schedule_prio_change(mrtk_task_t *task, mrtk_u32_t prio)
{
    _mrtk_schedule_remove_task(task);
    task->priority = prio;
    _mrtk_schedule_insert_task(task);
}

mrtk_void_t mrtk_tick_increase(mrtk_void_t)
{
    mrtk_base_t level = mrtk_hw_interrupt_disable();
    g_mrtk_tick++;

#if (MRTK_USING_TIMER == 1)
    /* 扫描硬定时器链表，触发到期的硬定时器回调 */
    mrtk_timer_hard_check();
#endif

    /* 时间片轮转实现 */
    if (g_CurrentTCB != MRTK_NULL) {
        if (g_CurrentTCB->remain_tick > 0) {
            /* 时间片递减 */
            g_CurrentTCB->remain_tick--;

            if (g_CurrentTCB->remain_tick == 0) {
                /* 重置当前任务的时间片 */
                g_CurrentTCB->remain_tick = g_CurrentTCB->init_tick;

                /* 只有当同优先级队列中有多个任务时，才触发轮转调度 */
                mrtk_list_node_t *head = &g_ready_task_list[g_CurrentTCB->priority];
                if (head->next->next != head) {
                    /* 将当前任务移到同优先级队列的末尾 */
                    _mrtk_list_remove(&g_CurrentTCB->sched_node);
                    _mrtk_list_insert_before(head, &g_CurrentTCB->sched_node);

                    /* 设置延迟调度标志，将在退出中断时触发调度 */
                    g_need_schedule = MRTK_TRUE;
                }
            }
        }
    }

    mrtk_hw_interrupt_enable(level);
}
