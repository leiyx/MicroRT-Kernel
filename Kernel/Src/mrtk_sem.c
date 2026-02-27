/**
 * @file mrtk_sem.c
 * @brief 信号量实现
 * @details 提供资源计数和任务间同步功能
 */

#include "mrtk_sem.h"
#if (MRTK_USING_SEMAPHORE == 1)

#include "mrtk_ipc_obj.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"
#include "mrtk_timer.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

mrtk_err_t mrtk_sem_init(mrtk_sem_t *sem, const mrtk_char_t *name, mrtk_u16_t value, mrtk_u8_t flag)
{
    if (sem == MRTK_NULL || name == MRTK_NULL) {
        return MRTK_EINVAL;
    }
    _mrtk_ipc_obj_init(&sem->ipc_obj, MRTK_OBJECT_TYPE_STATIC | MRTK_OBJ_TYPE_SEM, flag, name);
    sem->value = value;
    return MRTK_EOK;
}

mrtk_err_t mrtk_sem_detach(mrtk_sem_t *sem)
{
    if (sem == MRTK_NULL || MRTK_OBJ_IS_DYNAMIC(sem->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    if (_mrtk_ipc_obj_delete(sem) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_sem_t *mrtk_sem_create(const mrtk_char_t *name, mrtk_u16_t value, mrtk_u8_t flag)
{
    if (name == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_sem_t *sem = (mrtk_sem_t *) mrtk_malloc(sizeof(mrtk_sem_t));
    if (sem == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_err_t ret = mrtk_sem_init(sem, name, value, flag);
    if (ret != MRTK_EOK) {
        /* 初始化失败，释放已分配的内存 */
        mrtk_free(sem);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(sem->ipc_obj.obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return sem;
}

mrtk_err_t mrtk_sem_delete(mrtk_sem_t *sem)
{
    if (sem == MRTK_NULL || MRTK_OBJ_IS_STATIC(sem->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    if (_mrtk_ipc_obj_delete(sem) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    mrtk_free(sem);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_sem_take(mrtk_sem_t *sem, mrtk_s32_t timeout)
{
    if (sem == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        return MRTK_E_IN_ISR;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 1. 有信号量 */
    if (sem->value > 0) {
        --sem->value;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 2. 无信号量，不阻塞 */
    if (timeout == 0) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 3. 无信号量，阻塞 */
    mrtk_task_t *task = mrtk_task_self();
    task->last_error  = MRTK_EOK;

    _mrtk_ipc_suspend_one(&sem->ipc_obj.suspend_taker, sem->ipc_obj.flag, task);

    if (timeout > 0) {
        mrtk_timer_control(&task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
        mrtk_timer_start(&task->timer);
    }

    mrtk_hw_interrupt_enable(level);
    mrtk_schedule();

    return task->last_error;
}

mrtk_err_t mrtk_sem_release(mrtk_sem_t *sem)
{
    if (sem == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();
    /* 1. sem 没有消费者 */
    if (_mrtk_list_is_empty(&sem->ipc_obj.suspend_taker)) {
        if (sem->value < MRTK_SEM_MAX_VALUE) {
            ++sem->value;
        } else {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EFULL;
        }
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 2. sem 有消费者 */
    /* 唤醒阻塞链表头节点任务,直接移交信号量，不修改资源计数 */
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (_mrtk_ipc_resume_one(&sem->ipc_obj.suspend_taker, MRTK_EOK) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_err_t mrtk_sem_control(mrtk_sem_t *sem, mrtk_u32_t cmd, mrtk_void_t *arg)
{
    if (sem == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    switch (cmd) {
    /* 查询当前信号量计数值 */
    case MRTK_SEM_CMD_GET_VALUE: {
        if (arg == MRTK_NULL) {
            return MRTK_EINVAL;
        }
        mrtk_base_t level    = mrtk_hw_interrupt_disable();
        *(mrtk_u16_t *) arg = sem->value;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    /* 未实现的命令 */
    default:
        return MRTK_ERROR;
    }
}

#if (MRTK_DEBUG == 1)

/* ==============================================================================
 * 调试导出 API 实现
 * ============================================================================== */

mrtk_void_t mrtk_sem_dump(mrtk_sem_t *sem)
{
    if (sem == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 读取关键信息 */
    const mrtk_char_t *name        = sem->ipc_obj.obj.name;
    mrtk_u16_t         value       = sem->value;
    mrtk_u32_t         suspend_cnt = _mrtk_list_len(&sem->ipc_obj.suspend_taker);
    mrtk_u8_t          is_dynamic  = MRTK_OBJ_IS_DYNAMIC(sem->ipc_obj.obj.type);

    mrtk_hw_interrupt_enable(level);

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Semaphore Object Dump]\r\n");
    mrtk_printf("  Name        : %s\r\n", name);
    mrtk_printf("  Type        : SEMAPHORE\r\n");
    mrtk_printf("  Address     : 0x%p\r\n", sem);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出信号量信息 */
    mrtk_printf("  Value       : %u (resources available)\r\n", value);
    mrtk_printf("  Max Value   : %u\r\n", MRTK_SEM_MAX_VALUE);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出等待队列信息 */
    mrtk_printf("  Waiting Tasks : %u\r\n", suspend_cnt);
    mrtk_printf("  Wake Strategy : %s\r\n",
                (sem->ipc_obj.flag == MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO) ? "FIFO" : "PRIO");
    mrtk_printf("  Alloc Type    : %s\r\n", is_dynamic ? "DYNAMIC" : "STATIC");
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_SEMAPHORE == 1) */