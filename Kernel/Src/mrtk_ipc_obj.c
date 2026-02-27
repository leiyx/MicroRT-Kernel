/**
 * @file mrtk_ipc_obj.c
 * @author leiyx
 * @brief IPC 对象基类实现
 * @details 实现 IPC 对象的阻塞/唤醒机制，支持 FIFO 和优先级两种唤醒策略
 * @copyright Copyright (c) 2026
 */

#include "mrtk_ipc_obj.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_typedef.h"

/* ==============================================================================
 * IPC 对象构造与析构
 * ============================================================================== */

/* 需外部调用者负责关中断 */
mrtk_void_t _mrtk_ipc_obj_init(mrtk_void_t *obj, mrtk_u8_t type, mrtk_u8_t flag,
                               const mrtk_char_t *name)
{
    /* 初始化内核对象基类 */
    _mrtk_obj_init(obj, type, flag, name);

    /* 初始化阻塞任务链表 */
    _mrtk_list_init(&((mrtk_ipc_obj_t *) obj)->suspend_taker);
}

/* 需外部调用者负责关中断 */
mrtk_bool_t _mrtk_ipc_obj_delete(mrtk_void_t *obj)
{
    mrtk_ipc_obj_t *ipc           = (mrtk_ipc_obj_t *) obj;
    mrtk_bool_t     need_schedule = _mrtk_ipc_resume_all(&ipc->suspend_taker, MRTK_EDELETED);

    _mrtk_obj_delete(obj);

    return need_schedule;
}

/* ==============================================================================
 * IPC 对象内部函数实现
 * ============================================================================== */

mrtk_void_t _mrtk_ipc_suspend_one(mrtk_list_t *suspend_list, mrtk_u8_t flag, mrtk_task_t *task)
{
    _mrtk_schedule_remove_task(task);
    task->state = MRTK_TASK_STAT_SUSPEND;

    if (flag & MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO) {
        /* FIFO 策略：插入到队列尾部 */
        _mrtk_list_insert_before(suspend_list, &task->sched_node);
    } else {
        /* 优先级策略：按优先级插入，保持队列按优先级降序排列 */
        mrtk_task_t *cur_task;
        mrtk_list_t *insert_pos = suspend_list; /* 默认插入到尾部 */

        MRTK_LIST_FOR_EACH(cur_task, suspend_list, mrtk_task_t, sched_node)
        {
            /* 找到第一个优先级低于当前任务的位置 */
            if (mrtk_schedule_prio_lt(cur_task, task)) {
                insert_pos = &cur_task->sched_node;
                break;
            }
        }
        _mrtk_list_insert_before(insert_pos, &task->sched_node);
    }
}

mrtk_bool_t _mrtk_ipc_resume_one(mrtk_list_t *suspend_list, mrtk_err_t error)
{
    if (_mrtk_list_is_empty(suspend_list)) {
        return MRTK_FALSE;
    }
    mrtk_u8_t need_schedule = MRTK_FALSE;

    /* 从队列头部获取任务 */
    mrtk_list_node_t *node      = suspend_list->next;
    mrtk_task_t      *node_task = _mrtk_list_entry(node, mrtk_task_t, sched_node);

    node_task->last_error = error;
    /* 从阻塞队列中移除任务 */
    _mrtk_list_remove(node);

    /* 停止任务的超时定时器 */
#if (MRTK_USING_TIMER == 1)
    mrtk_timer_stop(&node_task->timer);
#endif

    /* 加入调度器 */
    _mrtk_schedule_insert_task(node_task);
    node_task->state = MRTK_TASK_STAT_READY;

    if (mrtk_schedule_prio_ht(node_task, g_CurrentTCB)) {
        need_schedule = MRTK_TRUE;
    }

    return need_schedule;
}

mrtk_u8_t _mrtk_ipc_resume_all(mrtk_list_t *suspend_list, mrtk_err_t error)
{
    mrtk_task_t *task;
    mrtk_task_t *next;
    mrtk_bool_t  need_schedule = MRTK_FALSE;

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    MRTK_LIST_FOR_EACH_SAFE(task, next, suspend_list, mrtk_task_t, sched_node)
    {
        task->last_error = error;
        _mrtk_list_remove(&task->sched_node);

#if (MRTK_USING_TIMER == 1)
        mrtk_timer_stop(&task->timer);
#endif

        _mrtk_schedule_insert_task(task);
        task->state = MRTK_TASK_STAT_READY;

        if (mrtk_schedule_prio_ht(task, g_CurrentTCB)) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    return need_schedule;
}