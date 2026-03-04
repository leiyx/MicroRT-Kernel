/**
 * @file mrtk_mutex.c
 * @brief 互斥量实现
 * @details 提供互斥访问功能，支持优先级继承防止优先级反转
 * @note 支持递归锁（同任务可多次获取）、优先级继承协议
 * @note 支持多互斥量场景：任务持有多个互斥量时，优先级恢复算法完整
 * @copyright Copyright (c) 2026
 */

#include "mrtk_mutex.h"

#if (MRTK_USING_MUTEX == 1)

#include "mrtk_ipc_obj.h"
#include "mrtk_irq.h"
#include "mrtk_mem_heap.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

/* ==============================================================================
 * 内部辅助函数
 * ============================================================================== */
/**
 * @brief 查找任务持有互斥量中的最高继承优先级
 * @details 遍历任务持有的所有互斥量，找出等待队列中最高优先级任务的优先级
 * @note 用于多互斥量场景下的优先级恢复
 * @param[in] task 任务指针
 * @return mrtk_u8_t 最高继承优先级（如果无等待者，返回任务初始优先级）
 */
static mrtk_u8_t _mrtk_mutex_find_highest_inherited_prio(mrtk_task_t *task)
{
    mrtk_u8_t     highest_prio = task->orig_prio; /* 默认为任务的原始优先级 */
    mrtk_mutex_t *mutex;
    mrtk_task_t  *waiter;

    MRTK_LIST_FOR_EACH(mutex, &task->held_mutex_list, mrtk_mutex_t, held_node)
    {
        /* 检查此互斥量的等待队列是否有任务 */
        MRTK_LIST_FOR_EACH(waiter, &mutex->ipc_obj.suspend_taker, mrtk_task_t, sched_node)
        {
            /* 找到优先级最高的等待者 */
            if (mrtk_schedule_prio_ht(waiter, (mrtk_task_t *) &highest_prio)) {
                highest_prio = waiter->priority;
            }
        }
    }

    return highest_prio;
}

/* ==============================================================================
 * 生命周期管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_mutex_init(mrtk_mutex_t *mutex, const mrtk_char_t *name, mrtk_u8_t flag)
{
    if (mutex == MRTK_NULL || name == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    _mrtk_ipc_obj_init(mutex, MRTK_OBJECT_TYPE_STATIC | MRTK_OBJ_TYPE_MUTEX, flag, name);

    mutex->value      = (mrtk_u16_t) MRTK_MUTEX_STATE_UNLOCKED;
    mutex->orig_prio  = MRTK_INVALID_U8;
    mutex->nest       = 0;
    mutex->owner_task = MRTK_NULL;
    _mrtk_list_init(&mutex->held_node);

    return MRTK_EOK;
}

mrtk_err_t mrtk_mutex_detach(mrtk_mutex_t *mutex)
{
    /* 动态对象不能通过 delete 释放 */
    if (mutex == MRTK_NULL || MRTK_OBJ_IS_DYNAMIC(mutex->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    if (mutex->owner_task != MRTK_NULL) {
        /* 从任务的持有列表中移除此互斥量（必须在恢复优先级之前） */
        _mrtk_list_remove(&mutex->held_node);

        /* 恢复任务优先级（考虑该任务可能还持有其他锁） */
        mrtk_u8_t restore_prio = _mrtk_mutex_find_highest_inherited_prio(mutex->owner_task);
        /* 如果优先级发生了变化，可能需要重新调度 */
        if (mutex->owner_task->priority != restore_prio) {
            _mrtk_schedule_prio_change(mutex->owner_task, restore_prio);
            need_schedule = MRTK_TRUE;
        }
    }
    if (_mrtk_ipc_obj_delete(mutex) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_mutex_t *mrtk_mutex_create(const mrtk_char_t *name, mrtk_u8_t flag)
{
    if (name == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_mutex_t *mutex = (mrtk_mutex_t *) mrtk_malloc(sizeof(mrtk_mutex_t));
    if (mutex == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_err_t ret = mrtk_mutex_init(mutex, name, flag);
    if (ret != MRTK_EOK) {
        /* 初始化失败，释放已分配的内存 */
        mrtk_free(mutex);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(mutex->ipc_obj.obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return mutex;
}

mrtk_err_t mrtk_mutex_delete(mrtk_mutex_t *mutex)
{
    /* 静态对象不能通过 delete 释放 */
    if (mutex == MRTK_NULL || MRTK_OBJ_IS_STATIC(mutex->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }
    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    if (mutex->owner_task != MRTK_NULL) {
        /* 从任务的持有列表中移除此互斥量 */
        _mrtk_list_remove(&mutex->held_node);

        /* 恢复任务优先级（考虑该任务可能还持有其他锁） */
        mrtk_u8_t restore_prio = _mrtk_mutex_find_highest_inherited_prio(mutex->owner_task);
        /* 如果优先级发生了变化，可能需要重新调度 */
        if (mutex->owner_task->priority != restore_prio) {
            _mrtk_schedule_prio_change(mutex->owner_task, restore_prio);
            need_schedule = MRTK_TRUE;
        }
    }
    if (_mrtk_ipc_obj_delete(mutex) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    mrtk_free(mutex);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

/* ==============================================================================
 * 核心操作 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_mutex_take(mrtk_mutex_t *mutex, mrtk_u32_t timeout)
{
    if (mutex == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 不允许在中断中阻塞 */
    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        return MRTK_E_IN_ISR;
    }

    mrtk_task_t *cur_task = mrtk_task_self();

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 1. 递归锁场景：当前任务已持有此互斥量 */
    if (cur_task == mutex->owner_task) {
        ++mutex->nest;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 2. 互斥量未加锁：直接获取成功 */
    if (mutex->value == MRTK_MUTEX_STATE_UNLOCKED) {
        mutex->value      = (mrtk_u16_t) MRTK_MUTEX_STATE_LOCKED;
        mutex->orig_prio  = cur_task->priority;
        mutex->nest       = 1;
        mutex->owner_task = cur_task;

        _mrtk_list_insert_before(&cur_task->held_mutex_list, &mutex->held_node);

        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 3. 互斥量已加锁：非阻塞模式 */
    if (timeout == 0) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_EEMPTY;
    }

    /* 4. 互斥量已加锁：阻塞模式 */
    cur_task->last_error = MRTK_EOK;
    if (mrtk_schedule_prio_ht(cur_task, mutex->owner_task)) {
        /* 优先级继承协议：防止优先级反转 */
        /* 优先级继承：如果当前任务优先级高于持有者，提升持有者优先级 */
        _mrtk_schedule_prio_change(mutex->owner_task, cur_task->priority);
    }

    /* 从就绪队列移除 + 挂入等待队列 */
    _mrtk_ipc_suspend_one(&mutex->ipc_obj.suspend_taker, mutex->ipc_obj.obj.flag, cur_task);

    /* 设置超时定时器 */
    if (timeout != MRTK_WAIT_FOREVER) {
        mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
        mrtk_timer_start(&cur_task->timer);
    }

    mrtk_hw_interrupt_enable(level);

    mrtk_schedule();

    /*  任务被唤醒后，返回结果（last_error 由唤醒者设置） */
    /*  可能的错误码： */
    /*    - MRTK_EOK：获取成功（被 release 唤醒） */
    /*    - MRTK_EDELETED：互斥量被删除（被 detach/delete 唤醒） */

    return cur_task->last_error;
}

mrtk_err_t mrtk_mutex_release(mrtk_mutex_t *mutex)
{
    if (mutex == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 中断上下文检查：Mutex 不允许在中断中释放 */
    if (mrtk_irq_get_nest() != 0) {
        /* 带有所有权概念的 Mutex 不适合中断场景 */
        /* 如需中断同步，使用信号量（Semaphore） */
        return MRTK_E_IN_ISR;
    }

    mrtk_task_t *cur_task = mrtk_task_self();
    if (cur_task == MRTK_NULL) {
        return MRTK_ERROR;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 所有权检查：只有持有者才能释放 */
    if (cur_task != mutex->owner_task) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 1. 递归锁处理：嵌套计数减 1 */
    if (--mutex->nest > 0) {
        /* 仍持有锁，不释放 */
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 2. 互斥量无等待者：直接释放 */
    if (_mrtk_list_is_empty(&mutex->ipc_obj.suspend_taker)) {
        /* 从任务的持有列表中移除此互斥量 */
        _mrtk_list_remove(&mutex->held_node);

        /* 恢复当前任务优先级（考虑该任务可能还持有其他锁） */
        mrtk_u8_t restore_prio = _mrtk_mutex_find_highest_inherited_prio(cur_task);
        if (cur_task->priority != restore_prio) {
            _mrtk_schedule_prio_change(cur_task, restore_prio);
        }

        mutex->orig_prio  = MRTK_INVALID_U8;
        mutex->value      = (mrtk_u16_t) MRTK_MUTEX_STATE_UNLOCKED;
        mutex->owner_task = MRTK_NULL;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 3. 互斥量有消费者 */

    /* 从任务的持有列表中移除此互斥量（必须在恢复优先级之前） */
    _mrtk_list_remove(&mutex->held_node);

    /* 恢复当前任务优先级 */
    /*     多互斥量场景的完整实现： */
    /*     遍历任务持有的所有互斥量，找出等待队列中最高优先级任务的优先级 */
    /*     作为恢复目标。如果其他互斥量无等待者，则恢复到初始优先级。 */
    /*     - 任务 A 持有 Mutex1 和 Mutex2 */
    /*     - Mutex1 等待队列：优先级 5 的任务 B */
    /*     - Mutex2 等待队列：优先级 10 的任务 C */
    /*     - 释放 Mutex2 后，任务 A 的优先级应恢复到 5（继承自 Mutex1 的等待者） */
    mrtk_u8_t restore_prio = _mrtk_mutex_find_highest_inherited_prio(cur_task);
    _mrtk_schedule_prio_change(cur_task, restore_prio);

    mrtk_task_t *next_task =
        _mrtk_list_entry(mutex->ipc_obj.suspend_taker.next, mrtk_task_t, sched_node);

    /* 唤醒一个消费者 */
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (_mrtk_ipc_resume_one(&mutex->ipc_obj.suspend_taker, MRTK_EOK) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    /* 资源移交 */
    mutex->nest       = 1;
    mutex->orig_prio  = next_task->priority;
    mutex->owner_task = next_task;
    _mrtk_list_insert_before(&next_task->held_mutex_list, &mutex->held_node);

    /* 设置获取成功标志 */
    next_task->last_error = MRTK_EOK;

    mrtk_hw_interrupt_enable(level);

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

/* ==============================================================================
 * 控制接口实现
 * ============================================================================== */

mrtk_err_t mrtk_mutex_control(mrtk_mutex_t *mutex, mrtk_u32_t cmd, mrtk_void_t *arg)
{
    if (mutex == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    switch (cmd) {
    /* 查询持有者任务 */
    case MRTK_MUTEX_CMD_GET_OWNER: {
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        mrtk_base_t level     = mrtk_hw_interrupt_disable();
        *(mrtk_task_t **) arg = mutex->owner_task;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 查询递归嵌套深度 */
    case MRTK_MUTEX_CMD_GET_NEST: {
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        mrtk_base_t level  = mrtk_hw_interrupt_disable();
        *(mrtk_u8_t *) arg = mutex->nest;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 查询原始优先级 */
    case MRTK_MUTEX_CMD_GET_ORIG_PRIO: {
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        mrtk_base_t level  = mrtk_hw_interrupt_disable();
        *(mrtk_u8_t *) arg = mutex->orig_prio;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 未实现的命令 */
    default:
        return MRTK_ERROR;
    }
}

mrtk_bool_t _mrtk_mutex_force_release(mrtk_mutex_t *mutex)
{
    /* 1. 强制重置状态，无视 owner */
    mutex->orig_prio  = MRTK_INVALID_U8;
    mutex->value      = (mrtk_u16_t) MRTK_MUTEX_STATE_UNLOCKED;
    mutex->owner_task = MRTK_NULL;
    mutex->nest       = 0;

    /* 2. 唤醒所有等待该互斥量的任务 */
    /* 如果有任务被唤醒且优先级高于当前，返回 TRUE 表示需要调度 */
    return _mrtk_ipc_resume_all(&mutex->ipc_obj.suspend_taker, MRTK_EDELETED);
}

#if (MRTK_DEBUG == 1)

/* ==============================================================================
 * 调试导出 API 实现
 * ============================================================================== */

/* 互斥量状态字符串映射表 */
static const mrtk_char_t *g_mutex_state_str[] = {
    "UNLOCKED", /**< MRTK_MUTEX_STATE_UNLOCKED */
    "LOCKED",   /**< MRTK_MUTEX_STATE_LOCKED */
};

/**
 * @brief 导出互斥量状态信息到控制台
 * @details 打印互斥量的名称、锁定状态、持有者、优先级继承等调试信息
 * @note 内部 API，请勿在应用代码中直接调用
 * @param[in] mutex 互斥量控制块指针
 */
mrtk_void_t mrtk_mutex_dump(mrtk_mutex_t *mutex)
{
    /* 防御性编程：检查空指针 */
    if (mutex == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 读取关键信息 */
    const mrtk_char_t *name        = mutex->ipc_obj.obj.name;
    mrtk_u16_t         value       = mutex->value;
    mrtk_u8_t          nest        = mutex->nest;
    mrtk_u8_t          orig_prio   = mutex->orig_prio;
    mrtk_task_t       *owner_task  = mutex->owner_task;
    mrtk_u32_t         suspend_cnt = _mrtk_list_len(&mutex->ipc_obj.suspend_taker);
    mrtk_u8_t          is_dynamic  = MRTK_OBJ_IS_DYNAMIC(mutex->ipc_obj.obj.type);

    mrtk_hw_interrupt_enable(level);

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Mutex Object Dump]\r\n");
    mrtk_printf("  Name        : %s\r\n", name);
    mrtk_printf("  Type        : MUTEX\r\n");
    mrtk_printf("  Address     : 0x%p\r\n", mutex);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出锁定状态信息 */
    const mrtk_char_t *state_str = (value < 2) ? g_mutex_state_str[value] : "UNKNOWN";
    mrtk_printf("  State       : %s\r\n", state_str);
    mrtk_printf("  Nest        : %u (recursive lock depth)\r\n", nest);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出持有者信息 */
    if (owner_task != MRTK_NULL) {
        mrtk_printf("  Owner Task  : %s (priority: %u)\r\n", owner_task->obj.name,
                    owner_task->priority);
        if (orig_prio != MRTK_INVALID_U8) {
            mrtk_printf("  Orig Priority: %u (priority inheritance occurred)\r\n", orig_prio);
        } else {
            mrtk_printf("  Orig Priority: N/A (no priority inheritance)\r\n");
        }
    } else {
        mrtk_printf("  Owner Task  : N/A (mutex is unlocked)\r\n");
        mrtk_printf("  Orig Priority: N/A\r\n");
    }
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出等待队列信息 */
    mrtk_printf("  Waiting Tasks : %u\r\n", suspend_cnt);
    mrtk_printf("  Wake Strategy : %s\r\n",
                (mutex->ipc_obj.flag == MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO) ? "FIFO" : "PRIO");
    mrtk_printf("  Alloc Type    : %s\r\n", is_dynamic ? "DYNAMIC" : "STATIC");
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_MUTEX == 1) */
