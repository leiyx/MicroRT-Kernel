/**
 * @file mrtk_msg_queue.c
 * @brief 消息队列实现
 * @details 提供变长消息传递功能，支持紧急消息和阻塞等待
 */

#include "mrtk_msg_queue.h"
#if (MRTK_USING_MESSAGE_QUEUE == 1)

#include "mrtk_errno.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"
#include "mrtk_timer.h"
#include "mrtk_typedef.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

mrtk_err_t mrtk_mq_init(mrtk_mq_t *mq, const mrtk_char_t *name, mrtk_void_t *msg_pool,
                        mrtk_size_t msg_size, mrtk_size_t pool_size, mrtk_u8_t flag)
{
    if (mq == MRTK_NULL || name == MRTK_NULL || msg_pool == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    _mrtk_ipc_obj_init(mq, MRTK_OBJECT_TYPE_STATIC | MRTK_OBJ_TYPE_QUEUE, flag, name);

    mq->msg_pool     = msg_pool;
    mq->max_msg_size = MRTK_ALIGN_UP(msg_size, MRTK_ALIGN_SIZE);

    mrtk_u32_t block_size = sizeof(mrtk_mq_msg_header_t) + mq->max_msg_size;
    mq->max_msg_cnt       = pool_size / block_size;
    if (mq->max_msg_cnt == 0) {
        return MRTK_EINVAL;
    }

    mq->cur_msg_cnt = 0;
    mq->head_msg    = MRTK_NULL;
    mq->tail_msg    = MRTK_NULL;
    mq->free_msg    = msg_pool;
    _mrtk_list_init(&mq->suspend_releaser);

    /* 消息链表初始化 */
    mrtk_mq_msg_header_t *cur = (mrtk_mq_msg_header_t *) msg_pool;
    for (mrtk_u16_t loop = 0; loop < mq->max_msg_cnt - 1; ++loop) {
        cur->next = (mrtk_mq_msg_header_t *) ((mrtk_ptr_t) cur + block_size);
        cur       = cur->next;
    }
    cur->next = MRTK_NULL;

    return MRTK_EOK;
}

mrtk_err_t mrtk_mq_detach(mrtk_mq_t *mq)
{
    if (mq == MRTK_NULL || MRTK_OBJ_IS_DYNAMIC(mq->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    /* 唤醒所有等待发送的任务 */
    if (_mrtk_ipc_resume_all(&mq->suspend_releaser, MRTK_EDELETED) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }
    if (_mrtk_ipc_obj_delete(mq) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_mq_t *mrtk_mq_create(const mrtk_char_t *name, mrtk_size_t msg_size, mrtk_size_t max_msgs,
                          mrtk_u8_t flag)
{
    mrtk_u32_t aligned_size = MRTK_ALIGN_UP(msg_size, MRTK_ALIGN_SIZE);
    mrtk_mq_t *mq           = (mrtk_mq_t *) mrtk_malloc(sizeof(mrtk_mq_t));
    if (mq == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_u32_t   pool_size = (aligned_size + sizeof(mrtk_mq_msg_header_t)) * max_msgs;
    mrtk_void_t *msg_pool  = mrtk_malloc(pool_size);
    if (msg_pool == MRTK_NULL) {
        mrtk_free(mq);
        return MRTK_NULL;
    }
    if (mrtk_mq_init(mq, name, msg_pool, msg_size, pool_size, flag) != MRTK_EOK) {
        mrtk_free(mq);
        mrtk_free(msg_pool);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(mq->ipc_obj.obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return mq;
}

mrtk_err_t mrtk_mq_delete(mrtk_mq_t *mq)
{
    if (mq == MRTK_NULL || MRTK_OBJ_IS_STATIC(mq->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    /* 唤醒所有等待发送的任务 */
    if (_mrtk_ipc_resume_all(&mq->suspend_releaser, MRTK_EDELETED) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }
    if (_mrtk_ipc_obj_delete(mq) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    mrtk_free(mq->msg_pool);
    mrtk_free(mq);

    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_mq_send_wait(mrtk_mq_t *mq, const mrtk_void_t *buffer, mrtk_size_t size,
                             mrtk_s32_t timeout)
{
    mrtk_mq_msg_header_t *msg;
    mrtk_task_t          *cur_task;

    if (mq == MRTK_NULL || buffer == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();
    if (size > mq->max_msg_size) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 若队列中消息已满 */
    while (mq->free_msg == MRTK_NULL) {
        if (timeout == 0) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_ERROR;
        }

        cur_task             = mrtk_task_self();
        cur_task->last_error = MRTK_EOK;

        if (timeout > 0) {
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_ONESHOT, &timeout);
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
            mrtk_timer_start(&cur_task->timer);
        }

        _mrtk_ipc_suspend_one(&mq->suspend_releaser, mq->ipc_obj.obj.flag, cur_task);

        mrtk_hw_interrupt_enable(level);
        mrtk_schedule();
        level = mrtk_hw_interrupt_disable();

        if (cur_task->last_error != MRTK_EOK) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_ERROR;
        }
    }

    /* 维护free_msg */
    msg          = mq->free_msg;
    mq->free_msg = ((mrtk_mq_msg_header_t *) (mq->free_msg))->next;

    /* 开中断拷贝数据 */
    mrtk_hw_interrupt_enable(level);
    memcpy(msg + 1, buffer, size);
    level = mrtk_hw_interrupt_disable();

    ++mq->cur_msg_cnt;

    /* 维护head_msg、tail_msg */
    if (mq->cur_msg_cnt == 1) {
        mq->head_msg = msg;
        mq->tail_msg = msg;
    } else {
        mrtk_mq_msg_header_t *tail = (mrtk_mq_msg_header_t *) mq->tail_msg;
        tail->next                 = msg;
        mq->tail_msg               = msg;
    }
    msg->next = MRTK_NULL;

    /* 如果有等待者，唤醒等待者 */
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (!_mrtk_list_is_empty(&mq->ipc_obj.suspend_taker)) {
        if (_mrtk_ipc_resume_one(&mq->ipc_obj.suspend_taker, MRTK_EOK) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_err_t mrtk_mq_recv(mrtk_mq_t *mq, mrtk_void_t *buffer, mrtk_size_t size, mrtk_s32_t timeout)
{
    if (mq == MRTK_NULL || buffer == MRTK_NULL) {
        return MRTK_EINVAL;
    }
    mrtk_mq_msg_header_t *msg;
    mrtk_task_t          *cur_task;

    mrtk_base_t level = mrtk_hw_interrupt_disable();
    if (size > mq->max_msg_size) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    /* 中断中不能有非阻塞操作 */
    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_E_IN_ISR;
    }

    /* 若队列中无消息 */
    while (mq->cur_msg_cnt == 0) {
        if (timeout == 0) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_ERROR;
        }

        cur_task             = mrtk_task_self();
        cur_task->last_error = MRTK_EOK;

        if (timeout > 0) {
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
            mrtk_timer_start(&cur_task->timer);
        }

        _mrtk_ipc_suspend_one(&mq->ipc_obj.suspend_taker, mq->ipc_obj.flag, cur_task);

        mrtk_hw_interrupt_enable(level);
        mrtk_schedule();
        level = mrtk_hw_interrupt_disable();

        if (cur_task->last_error != MRTK_EOK) {
            mrtk_hw_interrupt_enable(level);
            return cur_task->last_error;
        }
    }

    --mq->cur_msg_cnt;

    /* 从队头取出消息，并更新队头 */
    msg          = mq->head_msg;
    mq->head_msg = ((mrtk_mq_msg_header_t *) (mq->head_msg))->next;

    /* 开中断，拷贝数据 */
    mrtk_hw_interrupt_enable(level);
    memcpy(buffer, msg + 1, size);
    level = mrtk_hw_interrupt_disable();

    /* 释放msg，通过头插法插入空闲消息链表，并更新free_msg */
    msg->next    = (mrtk_mq_msg_header_t *) mq->free_msg;
    mq->free_msg = msg;
    /* 特殊情况：队列里就一个消息，被取走就空了 */
    if (mq->head_msg == MRTK_NULL) {
        mq->tail_msg = MRTK_NULL;
    }

    mrtk_bool_t need_schedule = MRTK_FALSE;
    /* 如果有等待的发送者，唤醒发送者 */
    if (!_mrtk_list_is_empty(&mq->suspend_releaser)) {
        if (_mrtk_ipc_resume_one(&mq->suspend_releaser, MRTK_EOK) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_err_t mrtk_mq_control(mrtk_mq_t *mq, int cmd, mrtk_void_t *arg)
{
    if (mq == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    switch (cmd) {
    case MRTK_MQ_CMD_RESET: {
        /* 重置消息队列，清空所有消息 */
        if (arg != MRTK_NULL) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EINVAL;
        }

        /* 重置消息链表 */
        mq->head_msg    = MRTK_NULL;
        mq->tail_msg    = MRTK_NULL;
        mq->cur_msg_cnt = 0;

        /* 重新初始化空闲消息链表 */
        mrtk_u32_t            block_size = sizeof(mrtk_mq_msg_header_t) + mq->max_msg_size;
        mrtk_mq_msg_header_t *cur        = (mrtk_mq_msg_header_t *) mq->msg_pool;
        for (mrtk_u16_t loop = 0; loop < mq->max_msg_cnt - 1; ++loop) {
            cur->next = (mrtk_mq_msg_header_t *) ((mrtk_ptr_t) cur + block_size);
            cur       = cur->next;
        }
        cur->next    = MRTK_NULL;
        mq->free_msg = mq->msg_pool;

        /* 唤醒所有等待的发送者 */
        mrtk_bool_t need_schedule = MRTK_FALSE;
        if (_mrtk_ipc_resume_all(&mq->suspend_releaser, MRTK_EOK) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }

        mrtk_hw_interrupt_enable(level);

        if (need_schedule == MRTK_TRUE) {
            mrtk_schedule();
        }
        return MRTK_EOK;
    }

    case MRTK_MQ_CMD_GET_CUR_MSG_CNT: {
        /* 获取当前消息数量 */
        if (arg == MRTK_NULL) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EINVAL;
        }
        *(mrtk_u16_t *) arg = mq->cur_msg_cnt;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    case MRTK_MQ_CMD_GET_MAX_MSG_CNT: {
        /* 获取最大消息数量 */
        if (arg == MRTK_NULL) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EINVAL;
        }
        *(mrtk_u16_t *) arg = mq->max_msg_cnt;
        mrtk_hw_interrupt_enable(level);
        return MRTK_EOK;
    }

    default:
        mrtk_hw_interrupt_enable(level);
        return MRTK_EINVAL;
    }
}

#if (MRTK_DEBUG == 1)

/* ==============================================================================
 * 调试导出 API 实现
 * ============================================================================== */

/**
 * @brief 导出消息队列状态信息到控制台
 * @details 打印消息队列的容量、使用情况、等待队列等调试信息
 * @note 内部 API，请勿在应用代码中直接调用
 * @param[in] mq 消息队列控制块指针
 */
mrtk_void_t mrtk_mq_dump(mrtk_mq_t *mq)
{
    /* 防御性编程：检查空指针 */
    if (mq == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 读取关键信息 */
    const mrtk_char_t *name        = mq->ipc_obj.obj.name;
    mrtk_u32_t         max_cnt     = mq->max_msg_cnt;
    mrtk_u32_t         cur_cnt     = mq->cur_msg_cnt;
    mrtk_u32_t         max_size    = mq->max_msg_size;
    mrtk_u32_t         suspend_cnt = _mrtk_list_len(&mq->ipc_obj.suspend_taker);
    mrtk_u32_t         sender_cnt  = _mrtk_list_len(&mq->suspend_releaser);
    mrtk_u8_t          is_dynamic  = MRTK_OBJ_IS_DYNAMIC(mq->ipc_obj.obj.type);
    float              usage_ratio = (max_cnt > 0) ? ((cur_cnt * 100.0f) / max_cnt) : 0.0f;

    mrtk_hw_interrupt_enable(level);

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[MessageQueue Object Dump]\r\n");
    mrtk_printf("  Name           : %s\r\n", name);
    mrtk_printf("  Type           : MSG_QUEUE\r\n");
    mrtk_printf("  Address        : 0x%p\r\n", mq);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出消息池信息 */
    mrtk_printf("  MsgPool Addr   : 0x%p\r\n", mq->msg_pool);
    mrtk_printf("  Max Msg Size   : %u bytes\r\n", max_size);
    mrtk_printf("  Max Msg Count  : %u\r\n", max_cnt);
    mrtk_printf("  Cur Msg Count  : %u\r\n", cur_cnt);
    mrtk_printf("  Usage Ratio    : %.1f%%\r\n", usage_ratio);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出消息链表信息 */
    mrtk_printf("  HeadMsg Ptr    : 0x%p\r\n", mq->head_msg);
    mrtk_printf("  TailMsg Ptr    : 0x%p\r\n", mq->tail_msg);
    mrtk_printf("  FreeMsg Ptr    : 0x%p\r\n", mq->free_msg);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出等待队列信息 */
    mrtk_printf("  Waiting Tasks  : %u (waiting to receive)\r\n", suspend_cnt);
    mrtk_printf("  Waiting Tasks  : %u (waiting to send)\r\n", sender_cnt);
    mrtk_printf("  Wake Strategy  : %s\r\n",
                (mq->ipc_obj.flag == MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO) ? "FIFO" : "PRIO");
    mrtk_printf("  Alloc Type     : %s\r\n", is_dynamic ? "DYNAMIC" : "STATIC");
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_MESSAGE_QUEUE == 1) */