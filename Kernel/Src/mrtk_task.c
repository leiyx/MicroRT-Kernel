/**
 * @file mrtk_task.c
 * @author leiyx
 * @brief 任务管理模块实现
 * @details 实现任务的创建、删除、启动、挂起、恢复、延时等功能
 * @copyright Copyright (c) 2026
 */

#include "mrtk_task.h"
#include "cpu_port.h"
#include "mrtk_config.h"
#include "mrtk_errno.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_mutex.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_timer.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif
#include <string.h>

/* ==============================================================================
 * 外部全局变量引用
 * ============================================================================== */

/**
 * @brief 外部引用：就绪任务队列数组
 */
extern mrtk_list_node_t g_ready_task_list[MRTK_MAX_PRIO_LEVEL_NUM];

/**
 * @brief 外部引用：就绪任务优先级位图
 */
extern volatile mrtk_u32_t g_ready_prio_bitmap;

/**
 * @brief 外部引用：已终止任务的链表头
 */
extern mrtk_list_t g_defunct_task_list;

/* ==============================================================================
 * 内部辅助函数
 * ============================================================================== */

mrtk_bool_t _mrtk_task_cleanup(mrtk_task_t *task)
{
    mrtk_bool_t need_schedule = MRTK_FALSE;
    /* 停止内置定时器 */
#if (MRTK_USING_TIMER == 1)
    mrtk_timer_stop(&task->timer);
#endif

    /* 释放任务持有的所有互斥量 */
#if (MRTK_USING_MUTEX == 1)

    mrtk_mutex_t *mutex_pos, *mutex_next;
    MRTK_LIST_FOR_EACH_SAFE(mutex_pos, mutex_next, &task->held_mutex_list, mrtk_mutex_t, held_node)
    {
        _mrtk_list_remove(&mutex_pos->held_node);
        /* 调用底层强拆接口，累加调度请求 */
        if (_mrtk_mutex_force_release(mutex_pos) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }
    }

#endif

    /* 根据任务状态处理调度节点 */
    if (task->state == MRTK_TASK_STAT_READY || task->state == MRTK_TASK_STAT_RUNNING) {
        _mrtk_schedule_remove_task(task);
    } else if (task->state == MRTK_TASK_STAT_SUSPEND) {
        _mrtk_list_remove(&task->sched_node);
    }
    /* MRTK_TASK_STAT_INIT 不需要处理调度节点 */

    /* 修改任务状态为 CLOSE */
    task->state = MRTK_TASK_STAT_CLOSE;

    return need_schedule;
}

/**
 * @brief 内置定时器回调函数
 * @details 用于任务延时超时后唤醒任务
 * @param[in] para 任务控制块指针
 */
static mrtk_void_t _mrtk_task_timer_callback(mrtk_void_t *para)
{
    mrtk_task_t *task          = (mrtk_task_t *) para;
    mrtk_bool_t  need_schedule = MRTK_FALSE;

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 从定时器列表中移除任务 */
    _mrtk_list_remove(&task->sched_node);

    /* 将任务加入就绪队列 */
    _mrtk_schedule_insert_task(task);
    task->state = MRTK_TASK_STAT_READY;

    if (mrtk_schedule_prio_ht(task, mrtk_task_self()) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 在临界区外执行调度 */
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
}

/* ==============================================================================
 * 生命周期管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_task_init(const mrtk_char_t *name, mrtk_task_t *task, mrtk_task_entry_t entry,
                          mrtk_void_t *para, mrtk_u32_t *stack_base, mrtk_u32_t stack_size,
                          mrtk_u8_t priority, mrtk_tick_t tick)
{
    if (task == MRTK_NULL || entry == MRTK_NULL || stack_base == MRTK_NULL ||
        priority >= MRTK_MAX_PRIO_LEVEL_NUM) {
        return MRTK_EINVAL;
    }

    memset(task, 0, sizeof(mrtk_tcb_t));

    _mrtk_obj_init(&task->obj, MRTK_OBJ_TYPE_TASK | MRTK_OBJECT_TYPE_STATIC, 0, name);

    /* 初始化调度相关字段 */
    task->priority    = priority;
    task->state       = MRTK_TASK_STAT_INIT;
    task->init_tick   = (tick == 0) ? MRTK_TICK_PER_SECOND / 10 : tick;
    task->remain_tick = task->init_tick;

    /* 初始化原始优先级（如果启用互斥量） */
#if (MRTK_USING_MUTEX == 1)
    task->orig_prio = priority;
#endif

    /* 初始化栈相关字段 */
    task->stack_base      = stack_base;
    task->stack_size      = stack_size;
    task->task_entry      = entry;
    task->task_entry_para = para;

    /* 计算对齐后的栈顶 */
#if MRTK_ARCH_STACK_GROWS_DOWN
    /* 向下生长：基址 + 大小 = 顶端 */
    mrtk_ptr_t stack_top_val = (mrtk_ptr_t) stack_base + stack_size;
#else
    /* 向上生长：基址 = 顶端 */
    mrtk_ptr_t stack_top_val = (mrtk_ptr_t) stack_base;
#endif

    /* 使用移植层提供的宏，进行向下对齐 */
    stack_top_val = MRTK_ALIGN_DOWN(stack_top_val, MRTK_ARCH_STACK_ALIGN_SIZE);

    /* 调用移植层接口初始化硬件栈（设置 LR 为任务退出函数） */
    task->stack_ptr = (mrtk_u32_t *) mrtk_hw_stack_init(entry, para, (mrtk_void_t *) stack_top_val,
                                                        _mrtk_task_exit);

    /* 初始化链表节点 */
    _mrtk_list_init(&task->sched_node);

    /* 初始化内置定时器（如果启用） */
#if (MRTK_USING_TIMER == 1)
    /* 定时器对象已嵌入在 TCB 中，无需分配内存，但需要显式初始化 */
    mrtk_timer_init(&task->timer, "task_timer", _mrtk_task_timer_callback, task, 0,
                    MRTK_TIMER_FLAG_SOFT_TIMER);
#endif

    /* 初始化互斥量持有列表（如果启用） */
#if (MRTK_USING_MUTEX == 1)
    _mrtk_list_init(&task->held_mutex_list);
#endif

    /* 初始化错误和清理字段 */
    task->last_error           = MRTK_EOK;
    task->cleanup_handler      = MRTK_NULL;
    task->cleanup_handler_para = MRTK_NULL;

    /* 挂载到内核对象链表（需要临界区保护） */
    {
        mrtk_ubase_t level = mrtk_hw_interrupt_disable();
        _mrtk_list_insert_before(&g_obj_list[MRTK_OBJ_TYPE_TASK], &task->obj.obj_node);
        mrtk_hw_interrupt_enable(level);
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_task_detach(mrtk_task_t *task)
{
    if (task == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    if (task == mrtk_task_self()) {
        _mrtk_task_exit();
        return MRTK_EOK;
    }

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 1. 通用逻辑清理 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(task);

    /* 2. 手动取下对象节点，不挂入消亡链表*/
    _mrtk_obj_delete(task);

    mrtk_hw_interrupt_enable(level);

    /* 3. 他杀情形，最后调用用户清理回调函数 */
    if (task->cleanup_handler != MRTK_NULL) {
        task->cleanup_handler(task->cleanup_handler_para);
    }

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_task_t *mrtk_task_create(const mrtk_char_t *name, mrtk_task_entry_t entry, mrtk_void_t *para,
                              mrtk_u32_t stack_size, mrtk_u8_t priority, mrtk_tick_t tick)
{
#if (MRTK_USING_MEM_HEAP == 1)
    mrtk_task_t *task;
    mrtk_u32_t  *stack_base;

    if (entry == MRTK_NULL || stack_size == 0) {
        return MRTK_NULL;
    }

    if (priority >= MRTK_MAX_PRIO_LEVEL_NUM) {
        return MRTK_NULL;
    }

    /* 分配 TCB 内存 */
    task = (mrtk_task_t *) mrtk_malloc(sizeof(mrtk_tcb_t));
    if (task == MRTK_NULL) {
        return MRTK_NULL;
    }

    /* 分配栈内存 */
    stack_base = (mrtk_u32_t *) mrtk_malloc(stack_size);
    if (stack_base == MRTK_NULL) {
        mrtk_free(task);
        return MRTK_NULL;
    }

    /* 调用静态初始化函数 */
    mrtk_err_t ret =
        mrtk_task_init(name, task, entry, para, stack_base, stack_size, priority, tick);
    if (ret != MRTK_EOK) {
        mrtk_free(stack_base);
        mrtk_free(task);
        return MRTK_NULL;
    }

    /* 修改分配类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(task->obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return task;

#else
    /* 未启用堆内存管理 */
    (void) name;
    (void) entry;
    (void) para;
    (void) stack_size;
    (void) priority;
    (void) tick;
    return MRTK_NULL;
#endif
}

mrtk_err_t mrtk_task_delete(mrtk_task_t *task)
{
    if (task == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    if (task == mrtk_task_self()) {
        _mrtk_task_exit();
        return MRTK_EOK;
    }

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 1. 通用逻辑清理 */
    mrtk_bool_t need_schedule = _mrtk_task_cleanup(task);

    /* 2. 挂入消亡链表*/
    _mrtk_list_insert_before(&g_defunct_task_list, &task->sched_node);

    mrtk_hw_interrupt_enable(level);

    /* 3. 他杀情形，最后调用用户清理回调函数 */
    if (task->cleanup_handler != MRTK_NULL) {
        task->cleanup_handler(task->cleanup_handler_para);
    }

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

/* ==============================================================================
 * 核心调度与状态 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_task_start(mrtk_task_t *task)
{
    if (task == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_bool_t  need_schedule = MRTK_FALSE;
    mrtk_ubase_t level         = mrtk_hw_interrupt_disable();

    /* 检查任务状态 */
    if (task->state != MRTK_TASK_STAT_INIT) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 将任务加入就绪队列 */
    _mrtk_schedule_insert_task(task);
    task->state = MRTK_TASK_STAT_READY;

    need_schedule = MRTK_FALSE;
    if (mrtk_schedule_prio_ht(task, g_CurrentTCB)) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一调度 */
    if (need_schedule) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_task_t *mrtk_task_self(mrtk_void_t)
{
    return g_CurrentTCB;
}

mrtk_err_t mrtk_task_suspend(mrtk_task_t *task)
{
    /* 处理 MRTK_NULL 参数 */
    if (task == MRTK_NULL) {
        task = mrtk_task_self();
    }

    mrtk_bool_t  need_schedule = MRTK_FALSE;
    mrtk_ubase_t level         = mrtk_hw_interrupt_disable();

    /* 检查任务状态 */
    if (task->state != MRTK_TASK_STAT_READY && task->state != MRTK_TASK_STAT_RUNNING) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 从就绪队列中移除任务 */
    _mrtk_schedule_remove_task(task);
    /* 调度节点变成游离状态 */
    _mrtk_list_init(&task->sched_node);
    /* 更新任务状态 */
    task->state = MRTK_TASK_STAT_SUSPEND;

    /* 如果挂起的是当前任务，触发调度 */
    if (task == g_CurrentTCB) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_task_resume(mrtk_task_t *task)
{
    if (task == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_bool_t  need_schedule = MRTK_FALSE;
    mrtk_ubase_t level         = mrtk_hw_interrupt_disable();

    /* 检查任务状态 */
    if (task->state != MRTK_TASK_STAT_SUSPEND) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 如果任务并非由suspend api引起阻塞，则需要将调度节点从定时器链表上取下 */
#if (MRTK_USING_TIMER == 1)
    mrtk_timer_stop(&task->timer);
#endif

    /* 将任务加入就绪队列 */
    _mrtk_schedule_insert_task(task);
    task->state = MRTK_TASK_STAT_READY;

    if (mrtk_schedule_prio_ht(task, g_CurrentTCB)) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_task_yield(mrtk_void_t)
{
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 不能在中断上下文中使用 */
    if (g_interrupt_nest > 0) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 获取当前任务的优先级和就绪队列 */
    mrtk_u8_t    prio       = g_CurrentTCB->priority;
    mrtk_list_t *ready_list = &g_ready_task_list[prio];

    /* 若同优先级队列中无其他任务，无需让出 */
    if (ready_list->next->next == ready_list) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 将当前任务从队列头部移到尾部 */
    _mrtk_list_remove(&g_CurrentTCB->sched_node);
    _mrtk_list_insert_before(ready_list, &g_CurrentTCB->sched_node);

    mrtk_hw_interrupt_enable(level);

    mrtk_schedule();

    return MRTK_EOK;
}

mrtk_err_t mrtk_task_delay(mrtk_tick_t tick)
{
#if (MRTK_USING_TIMER == 1)
    mrtk_task_t *task;

    /* 中断上下文不应调用阻塞api */
    if (g_interrupt_nest > 0) {
        return MRTK_E_IN_ISR;
    }

    /* 延时为 0 等同于 yield */
    if (tick == 0) {
        return mrtk_task_yield();
    }

    task = mrtk_task_self();

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    _mrtk_schedule_remove_task(task);
    task->state = MRTK_TASK_STAT_SUSPEND;

    /* 启动内置定时器 */
    mrtk_timer_stop(&task->timer);
    mrtk_timer_control(&task->timer, MRTK_TIMER_CMD_SET_TIME, &tick);
    mrtk_timer_control(&task->timer, MRTK_TIMER_CMD_SET_ONESHOT, MRTK_NULL);
    mrtk_timer_start(&task->timer);

    mrtk_hw_interrupt_enable(level);

    mrtk_schedule();

    return MRTK_EOK;

#else
    /* 未启用定时器模块 */
    (void) tick;
    return MRTK_ERROR;
#endif
}

mrtk_err_t mrtk_task_delay_until(mrtk_tick_t *last_wakeup, mrtk_tick_t increment)
{
#if (MRTK_USING_TIMER == 1)
    mrtk_tick_t now;

    if (last_wakeup == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 中断上下文不应调用阻塞api */
    if (g_interrupt_nest > 0) {
        return MRTK_E_IN_ISR;
    }

    now = mrtk_tick_get();

    /* 计算下一次唤醒时刻（绝对时间累加，防止漂移） */
    *last_wakeup += increment;

    /* 如果已经错过了唤醒时刻，立即返回 */
    if ((mrtk_s32_t) (now - *last_wakeup) >= 0) {
        return MRTK_EOK;
    }
    /* 计算需要延时的 tick 数 */
    mrtk_tick_t tick = *last_wakeup - now;

    /* 调用相对延时函数 */
    return mrtk_task_delay(tick);

#else
    /* 未启用定时器模块 */
    (void) last_wakeup;
    (void) increment;
    return MRTK_ERROR;
#endif
}

/* ==============================================================================
 * 属性控制 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_task_set_priority(mrtk_task_t *task, mrtk_u8_t priority)
{
    if (task == MRTK_NULL || priority >= MRTK_MAX_PRIO_LEVEL_NUM) {
        return MRTK_EINVAL;
    }

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    mrtk_u8_t need_schedule = MRTK_FALSE;

    if (task->state == MRTK_TASK_STAT_READY || task->state == MRTK_TASK_STAT_RUNNING) {
        _mrtk_schedule_remove_task(task);
        task->priority = priority;
        _mrtk_schedule_insert_task(task);
        task->state = MRTK_TASK_STAT_READY;
    } else {
        task->priority = priority;
    }

    if (mrtk_schedule_prio_ht(task, mrtk_task_self())) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_u8_t mrtk_task_get_priority(mrtk_task_t *task)
{
    if (task == MRTK_NULL) {
        return MRTK_INVALID_U8;
    }

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    mrtk_u8_t prio = task->priority;

    mrtk_hw_interrupt_enable(level);

    return prio;
}

/* ==============================================================================
 * 通用控制接口实现
 * ============================================================================== */

mrtk_err_t mrtk_task_control(mrtk_task_t *task, mrtk_u32_t cmd, mrtk_void_t *arg)
{
    if (task == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 根据命令分发 */
    switch (cmd) {
    case MRTK_TASK_CMD_SET_PRIORITY:
        /* 设置优先级 */
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        return mrtk_task_set_priority(task, *(mrtk_u8_t *) arg);

    case MRTK_TASK_CMD_GET_PRIORITY:
        /* 获取优先级 */
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        *(mrtk_u8_t *) arg = task->priority;
        return MRTK_EOK;

#if (MRTK_USING_TIMER == 1)
    case MRTK_TASK_CMD_GET_TIMER:
        *(mrtk_timer_t **) arg = &task->timer;
        return MRTK_EOK;
#endif

    case MRTK_TASK_CMD_SET_CLEANUP:
        /* 注册清理回调函数 */
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        task->cleanup_handler = (mrtk_cleanup_handler_t) arg;
        return MRTK_EOK;

    default:
        return MRTK_EINVAL;
    }
}

/* ==============================================================================
 * 内部 API 实现
 * ============================================================================== */

mrtk_void_t _mrtk_task_exit(mrtk_void_t)
{
    mrtk_task_t *task = mrtk_task_self();

    /* 1. 调用用户清理回调函数 */
    /* 自杀情形为防止幽灵任务，必须首先执行 */
    if (task->cleanup_handler != MRTK_NULL) {
        task->cleanup_handler(task->cleanup_handler_para);
    }

    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 2. 通用逻辑清理 */
    _mrtk_task_cleanup(task);

    if (MRTK_OBJ_IS_DYNAMIC(task->obj.type) == MRTK_TRUE) {
        /* 3. 挂入消亡链表*/
        _mrtk_list_insert_before(&g_defunct_task_list, &task->sched_node);

    } else {
        /* 3. 手动取下对象节点，不挂入消亡链表*/
        _mrtk_obj_delete(task);
    }

    mrtk_hw_interrupt_enable(level);

    /* 触发调度（永不返回） */
    mrtk_schedule();

    /* 永远不会执行到这里 */
}

/* ==============================================================================
 * 对象状态导出 API 实现
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/* 任务状态字符串映射表 */
static const mrtk_char_t *g_task_state_str[] = {
    "INIT",    /**< MRTK_TASK_STAT_INIT */
    "READY",   /**< MRTK_TASK_STAT_READY */
    "RUNNING", /**< MRTK_TASK_STAT_RUNNING */
    "SUSPEND", /**< MRTK_TASK_STAT_SUSPEND */
    "CLOSE",   /**< MRTK_TASK_STAT_CLOSE */
};

/* 错误码字符串映射表 */
static const mrtk_char_t *g_errno_str[] = {
    "EOK",      /**< MRTK_EOK */
    "ERROR",    /**< MRTK_ERROR */
    "EINVAL",   /**< MRTK_EINVAL */
    "EDELETED", /**< MRTK_EDELETED */
    "EFULL",    /**< MRTK_EFULL */
    "EEMPTY",   /**< MRTK_EEMPTY */
    "E_IN_ISR", /**< MRTK_E_IN_ISR */
    "EBUSY",    /**< MRTK_EBUSY */
};

/**
 * @brief 导出任务状态信息到控制台
 * @note 内部 API，请勿在应用代码中直接调用
 */
mrtk_void_t mrtk_task_dump(mrtk_task_t *task)
{
    mrtk_u32_t   stack_used;
    mrtk_u32_t   stack_percent;
    mrtk_char_t *state_str;
    mrtk_char_t *errno_str;

    if (task == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    /* 检查状态值是否合法 */
    state_str = (task->state < 5) ? (mrtk_char_t *) g_task_state_str[task->state]
                                  : (mrtk_char_t *) "UNKNOWN";

    /* 检查错误码是否合法 */
    errno_str = (task->last_error < 8) ? (mrtk_char_t *) g_errno_str[task->last_error]
                                       : (mrtk_char_t *) "UNKNOWN";

    /* 计算栈使用情况（栈向下生长，已使用 = 栈底 + 栈大小 - 栈顶） */
    stack_used = (mrtk_ptr_t) task->stack_base + task->stack_size - (mrtk_ptr_t) task->stack_ptr;

    /* 计算栈使用百分比（整数运算，避免浮点） */
    /* 防御性编程：防止除0异常（测试环境中可能 stack_size = 0） */
    stack_percent = (task->stack_size > 0) ? ((stack_used * 100) / task->stack_size) : 0;

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Task Object Dump]\r\n");
    mrtk_printf("  Name      : %s\r\n", task->obj.name);
    mrtk_printf("  Type      : TASK\r\n");
    mrtk_printf("  Address   : 0x%p\r\n", task);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出调度信息 */
    mrtk_printf("  Priority  : %u (0=Highest, 31=Lowest)\r\n", task->priority);
    mrtk_printf("  State     : %s\r\n", state_str);
    mrtk_printf("  TimeSlice : Init=%u, Remain=%u\r\n", task->init_tick, task->remain_tick);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出栈信息 */
    mrtk_printf("  StackBase : 0x%p\r\n", task->stack_base);
    mrtk_printf("  StackPtr  : 0x%p\r\n", task->stack_ptr);
    mrtk_printf("  StackSize : %u bytes\r\n", task->stack_size);
    mrtk_printf("  StackUsed : %u bytes (%u%%)\r\n", stack_used, stack_percent);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出错误和回调信息 */
    mrtk_printf("  LastError : %s (%u)\r\n", errno_str, task->last_error);
    mrtk_printf("  Entry     : 0x%p\r\n", task->task_entry);
    mrtk_printf("  Param     : 0x%p\r\n", task->task_entry_para);
    mrtk_printf(
        "================================================================================\r\n");
}

/**
 * @brief 导出系统中所有任务的统计信息
 * @details 遍历全局任务链表，打印所有任务的核心信息并输出统计汇总
 */
mrtk_void_t mrtk_task_dump_all(mrtk_void_t)
{
    mrtk_task_t *task;
    mrtk_u32_t   task_count       = 0;
    mrtk_u32_t   stat_count[5]    = {0}; /* 各状态任务计数 */
    mrtk_u32_t   total_stack_size = 0;
    mrtk_u32_t   total_stack_used = 0;
    mrtk_u32_t   max_stack_used   = 0;
    mrtk_task_t *max_stack_task   = MRTK_NULL;

    /* 打印表头 */
    mrtk_printf("\r\n");
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Task List - All Tasks in System]\r\n");
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("  ID   Name            Priority State     StackSize StackUsed  %% Entry      "
                "Param\r\n");
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 遍历所有任务对象 */
    MRTK_LIST_FOR_EACH(task, &g_obj_list[MRTK_OBJ_TYPE_TASK], mrtk_task_t, obj.obj_node)
    {
        /* 统计计数 */
        task_count++;
        if (task->state < 5) {
            stat_count[task->state]++;
        }

        /* 计算栈使用情况（栈向下生长，已使用 = 栈底 + 栈大小 - 栈顶） */
        mrtk_u32_t stack_used =
            (mrtk_ptr_t) task->stack_base + task->stack_size - (mrtk_ptr_t) task->stack_ptr;
        mrtk_u32_t stack_percent = (stack_used * 100) / task->stack_size;

        total_stack_size += task->stack_size;
        total_stack_used += stack_used;

        /* 记录最大栈使用任务 */
        if (stack_used > max_stack_used) {
            max_stack_used = stack_used;
            max_stack_task = task;
        }

        /* 打印任务信息 */
        mrtk_char_t *state_str = (task->state < 5) ? (mrtk_char_t *) g_task_state_str[task->state]
                                                   : (mrtk_char_t *) "UNKNOWN ";

        mrtk_printf("  %-3u %-16s %-9u %-9s %-9u %-9u %-3u%% 0x%p 0x%p\r\n", task_count,
                    task->obj.name, task->priority, state_str, task->stack_size, stack_used,
                    stack_percent, task->task_entry, task->task_entry_para);
    }

    mrtk_hw_interrupt_enable(level);

    /* 打印统计汇总 */
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("[Summary Statistics]\r\n");
    mrtk_printf("  Total Tasks      : %u\r\n", task_count);
    mrtk_printf("  INIT   Tasks     : %u\r\n", stat_count[0]);
    mrtk_printf("  READY  Tasks     : %u\r\n", stat_count[1]);
    mrtk_printf("  RUNNING Tasks    : %u\r\n", stat_count[2]);
    mrtk_printf("  SUSPEND Tasks    : %u\r\n", stat_count[3]);
    mrtk_printf("  CLOSE   Tasks    : %u\r\n", stat_count[4]);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    if (task_count > 0) {
        mrtk_u32_t avg_stack_used    = total_stack_used / task_count;
        mrtk_u32_t avg_stack_percent = (total_stack_used * 100) / total_stack_size;

        mrtk_printf("[Stack Usage Statistics]\r\n");
        mrtk_printf("  Total Stack Size  : %u bytes\r\n", total_stack_size);
        mrtk_printf("  Total Stack Used  : %u bytes\r\n", total_stack_used);
        mrtk_printf("  Avg Stack Used    : %u bytes (%u%%)\r\n", avg_stack_used, avg_stack_percent);

        if (max_stack_task != MRTK_NULL) {
            mrtk_u32_t max_stack_percent = (max_stack_used * 100) / max_stack_task->stack_size;
            mrtk_printf("  Max Stack Used     : %u bytes (%u%%) - Task: %s\r\n", max_stack_used,
                        max_stack_percent, max_stack_task->obj.name);
        }
    }

    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

/* ==============================================================================
 * 空闲任务管理实现
 * ============================================================================== */

/* 空闲任务栈大小（512 字节） */
#define MRTK_IDLE_TASK_STACK_SIZE 512

/* 空闲任务栈空间（静态分配） */
mrtk_u32_t g_idle_task_stack[MRTK_IDLE_TASK_STACK_SIZE];

/* 空闲任务 TCB（静态分配，始终存在于系统中） */
mrtk_tcb_t g_idle_tcb;

mrtk_void_t mrtk_idle_task_entry(mrtk_void_t *param)
{
    (void) param;

    while (1) {
        mrtk_base_t level = mrtk_hw_interrupt_disable();

        while (!_mrtk_list_is_empty(&g_defunct_task_list)) {
            /* 从消亡链表头取出一个任务 */
            mrtk_tcb_t *task = _mrtk_list_entry(g_defunct_task_list.next, mrtk_tcb_t, sched_node);
            _mrtk_obj_delete(task);
            _mrtk_list_remove(&task->sched_node);

            /* 开中断，为了实时性，free不应在中断上下文中执行 */
            mrtk_hw_interrupt_enable(level);

            /* 释放动态内存 - tcb、任务栈 */
            mrtk_free(task->stack_base);
            mrtk_free(task);

            level = mrtk_hw_interrupt_disable();
        }

        mrtk_hw_interrupt_enable(level);

        /* TODO: 添加空闲统计、低功耗模式等其他后台任务 */
    }
}

mrtk_task_t *mrtk_task_get_idle(mrtk_void_t)
{
    return &g_idle_tcb;
}

mrtk_err_t mrtk_task_init_idle(mrtk_void_t)
{
    mrtk_err_t ret;

    /* 创建空闲任务（始终存在于系统，最低优先级） */
    ret = mrtk_task_init("idle", &g_idle_tcb, mrtk_idle_task_entry, MRTK_NULL, g_idle_task_stack,
                         MRTK_IDLE_TASK_STACK_SIZE, MRTK_IDLE_PRIORITY, MRTK_TICK_MAX);
    if (ret != MRTK_EOK) {
        return ret;
    }

    /* 启动空闲任务 */
    _mrtk_schedule_insert_task(&g_idle_tcb);
    g_idle_tcb.state = MRTK_TASK_STAT_READY;

    return MRTK_EOK;
}

#if (MRTK_USING_TIMER_SOFT == 1)
/* ==============================================================================
 * 定时器守护任务管理实现
 * ============================================================================== */

/* 定时器守护任务栈空间（静态分配） */
mrtk_u32_t g_timer_daemon_task_stack[MRTK_TIMER_TASK_STACK_SIZE];

/* 定时器守护任务 TCB（静态分配，始终存在于系统中） */
mrtk_tcb_t g_timer_daemon_tcb;

mrtk_void_t mrtk_timer_daemon_entry(mrtk_void_t *param)
{
    (void) param;

    while (1) {
        /* 调用软定时器检查函数 */
        mrtk_timer_soft_check();

        /* 让出 CPU，允许其他任务运行 */
        /* 使用 yield 保证软定时器的响应性，同时避免 CPU 空转 */
        mrtk_task_yield();
    }
}

mrtk_task_t *mrtk_task_get_timer_daemon(mrtk_void_t)
{
    return &g_timer_daemon_tcb;
}

mrtk_err_t mrtk_task_init_timer_daemon(mrtk_void_t)
{
    mrtk_err_t ret;

    /* 创建定时器守护任务（始终存在于系统，优先级由配置决定） */
    ret = mrtk_task_init("timer_daemon", &g_timer_daemon_tcb, mrtk_timer_daemon_entry, MRTK_NULL,
                         g_timer_daemon_task_stack, MRTK_TIMER_TASK_STACK_SIZE,
                         MRTK_TIMER_TASK_PRIO, MRTK_TICK_MAX);
    if (ret != MRTK_EOK) {
        return ret;
    }

    /* 启动定时器守护任务 */
    _mrtk_schedule_insert_task(&g_timer_daemon_tcb);
    g_timer_daemon_tcb.state = MRTK_TASK_STAT_READY;

    return MRTK_EOK;
}
#endif /* (MRTK_USING_TIMER_SOFT == 1) */
