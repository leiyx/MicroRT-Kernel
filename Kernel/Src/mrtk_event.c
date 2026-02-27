/**
 * @file mrtk_event.c
 * @author leiyx
 * @brief 事件标志组管理模块实现
 * @details 实现事件标志同步机制，支持多对多任务同步、逻辑与/逻辑或等待、自动清除标志
 * @copyright Copyright (c) 2026
 */

#include "mrtk_event.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_schedule.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

/* ==============================================================================
 * 内部辅助函数
 * ============================================================================== */

/**
 * @brief 检查事件条件是否满足
 * @details 根据 option 参数判断当前事件集合是否满足等待条件
 * @param[in] current_set 当前已触发的事件集合
 * @param[in] wait_mask   等待的事件位掩码
 * @param[in] option      等待选项（AND/OR）
 * @return mrtk_bool_t MRTK_TRUE 表示条件满足，MRTK_FALSE 表示不满足
 */
static inline mrtk_bool_t _mrtk_event_check_condition(mrtk_u32_t current_set, mrtk_u32_t wait_mask,
                                                      mrtk_u8_t option)
{
    if (option & MRTK_EVENT_FLAG_AND) {
        /* 逻辑与：所有指定位都必须置 1 */
        return (current_set & wait_mask) == wait_mask ? MRTK_TRUE : MRTK_FALSE;
    } else {
        /* 逻辑或：任意指定位置 1 即可 */
        return (current_set & wait_mask) != 0 ? MRTK_TRUE : MRTK_FALSE;
    }
}

/* ==============================================================================
 * 生命周期管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_event_init(mrtk_event_t *event, const mrtk_char_t *name, mrtk_u8_t flag)
{
    if (event == MRTK_NULL || name == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    _mrtk_ipc_obj_init(event, MRTK_OBJ_TYPE_EVENT | MRTK_OBJECT_TYPE_STATIC, flag, name);
    event->set = 0;

    return MRTK_EOK;
}

mrtk_err_t mrtk_event_detach(mrtk_event_t *event)
{
    if (event == MRTK_NULL || MRTK_OBJ_IS_DYNAMIC(event->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (_mrtk_ipc_obj_delete(event) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_event_t *mrtk_event_create(const mrtk_char_t *name, mrtk_u8_t flag)
{
#if (MRTK_USING_MEM_HEAP == 1)
    if (name == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_event_t *event = (mrtk_event_t *) mrtk_malloc(sizeof(mrtk_event_t));
    if (event == MRTK_NULL) {
        return MRTK_NULL;
    }

    /* 初始化事件对象 */
    mrtk_err_t ret = mrtk_event_init(event, name, flag);
    if (ret != MRTK_EOK) {
        mrtk_free(event);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(event->ipc_obj.obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return event;
#else
    (void) name;
    (void) flag;
    return MRTK_NULL;
#endif
}

mrtk_err_t mrtk_event_delete(mrtk_event_t *event)
{
#if (MRTK_USING_MEM_HEAP == 1)
    if (event == MRTK_NULL || MRTK_OBJ_IS_STATIC(event->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (_mrtk_ipc_obj_delete(event) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);
    mrtk_free(event);
    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
#else
    (void) event;
    return MRTK_EINVAL;
#endif
}

/* ==============================================================================
 * 核心 IPC 通信 API 实现
 * ============================================================================== */

/**
 * @brief 发送事件标志
 * @details 1. 将新事件与当前事件按位或合并
 *          2. 遍历等待队列（因为可能唤醒多个任务）
 *          3. 对每个等待任务检查其等待条件是否满足
 *          4. 满足条件的任务从等待队列移除，恢复到就绪队列
 *          5. 根据 need_schedule 标志在临界区外触发调度
 */
mrtk_err_t mrtk_event_send(mrtk_event_t *event, mrtk_u32_t set)
{
    if (event == MRTK_NULL || set == 0x00) {
        return MRTK_EINVAL;
    }

    mrtk_bool_t need_schedule    = MRTK_FALSE;
    mrtk_u32_t  total_clear_mask = 0;
    mrtk_base_t level            = mrtk_hw_interrupt_disable();

    /* 将新事件与当前事件合并 */
    event->set |= set;

    /* 遍历等待队列，检查是否有任务的条件被满足，使用 SAFE 版本是因为在循环体内可能删除当前节点 */
    mrtk_task_t *task;
    mrtk_task_t *next;

    MRTK_LIST_FOR_EACH_SAFE(task, next, &event->ipc_obj.suspend_taker, mrtk_task_t, sched_node)
    {
        /* 检查该任务的等待条件是否满足 */
        if (_mrtk_event_check_condition(event->set, task->event_set, task->event_option)) {
            /* 条件满足，保存实际触发的事件 */
            if (task->event_recved != MRTK_NULL) {
                *(task->event_recved) = event->set & task->event_set;
            }

            /* 如果任务选择了自动清除，记录需要清除的位到总掩码中
             * 必须在循环外统一清除，避免影响后续任务的判断 */
            if (task->event_option & MRTK_EVENT_FLAG_CLEAR) {
                total_clear_mask |= task->event_set;
            }

            _mrtk_list_remove(&task->sched_node);

            /* 将任务恢复到就绪队列 */
            _mrtk_schedule_insert_task(task);
            task->last_error = MRTK_EOK;

            /* 标记需要触发调度 */
            need_schedule = MRTK_TRUE;
        }
    }

    /*  统一清除所有需要清除的事件位 */
    if (total_clear_mask != 0) {
        event->set &= ~total_clear_mask;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

/**
 * @brief 接收事件标志（阻塞等待）
 * @details 1. 检查当前事件集合是否已满足条件
 *          2. 如果满足，填入 recved，根据 option 决定是否清除标志，直接返回
 *          3. 如果不满足且 timeout == 0，立即返回超时
 *          4. 如果不满足且需要等待：将等待参数记录到 TCB，挂起任务，启动定时器
 *          5. 任务醒来后，检查 last_error 判断是超时还是成功接收
 */
mrtk_err_t mrtk_event_recv(mrtk_event_t *event, mrtk_u32_t set_to_wait, mrtk_u8_t option,
                           mrtk_tick_t timeout, mrtk_u32_t *recved)
{
    if (event == MRTK_NULL || set_to_wait == 0x00 || recved == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 不允许同时指定 AND 和 OR */
    if ((option & MRTK_EVENT_FLAG_AND) && (option & MRTK_EVENT_FLAG_OR)) {
        return MRTK_EINVAL;
    }

    /* 中断上下文检查：阻塞操作不允许在中断中调用 */
    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        return MRTK_E_IN_ISR;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 条件满足 */
    if (_mrtk_event_check_condition(event->set, set_to_wait, option)) {
        /* 填入实际触发的事件 */
        *recved = event->set & set_to_wait;

        if (option & MRTK_EVENT_FLAG_CLEAR) {
            event->set &= ~set_to_wait;
        }

        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 条件不满足，非阻塞模式 */
    if (timeout == 0) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_EEMPTY;
    }

    /* 条件不满足，需要阻塞等待 */
    mrtk_task_t *current_task  = mrtk_task_self();
    current_task->event_set    = set_to_wait;
    current_task->event_option = option;
    current_task->event_recved = recved;

    /* 将当前任务挂起到事件组的等待队列，使用对象的唤醒策略 */
    _mrtk_ipc_suspend_one(&event->ipc_obj.suspend_taker, event->ipc_obj.flag, current_task);

    /* 如果指定了超时，启动定时器 */
    if (timeout != MRTK_IPC_WAIT_FOREVER) {
#if (MRTK_USING_TIMER == 1)
        mrtk_timer_control(&current_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
        mrtk_timer_start(&current_task->timer);
#endif
    }

    mrtk_hw_interrupt_enable(level);
    mrtk_schedule();

    /* 任务被唤醒后，检查 last_error 判断是超时还是成功接收 */
    return current_task->last_error;
}

mrtk_err_t mrtk_event_control(mrtk_event_t *event, mrtk_u32_t cmd, mrtk_void_t *arg)
{
    if (event == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 根据命令执行相应操作 */
    switch (cmd) {
    case MRTK_EVENT_CMD_CLEAR: /**< 清空所有事件标志 */
    {
        mrtk_base_t level = mrtk_hw_interrupt_disable();
        event->set        = 0;
        mrtk_hw_interrupt_enable(level);
    }
        (void) arg;
        return MRTK_EOK;

    default:
        return MRTK_EINVAL;
    }
}

/* ==============================================================================
 * 调试导出 API 实现
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

mrtk_void_t mrtk_event_dump(mrtk_event_t *event)
{
    if (event == MRTK_NULL) {
        mrtk_printf("[Event Dump] Error: event pointer is MRTK_NULL\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    const mrtk_char_t *name  = event->ipc_obj.obj.name;
    mrtk_u32_t         set   = event->set;
    mrtk_u32_t         count = _mrtk_list_len(&event->ipc_obj.suspend_taker);

    mrtk_hw_interrupt_enable(level);

    /* 格式化打印事件信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Event Dump] %s\r\n", name);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("  Address      : 0x%p\r\n", (void *) event);
    mrtk_printf("  Current Set  : 0x%08X\r\n", (unsigned int) set);
    mrtk_printf("  Wait Tasks   : %u\r\n", (unsigned int) count);
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */
