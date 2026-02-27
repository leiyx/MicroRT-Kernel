/**
 * @file mrtk_mail_box.c
 * @brief 邮箱实现
 * @details 提供指针传递的消息传递功能，基于环形缓冲区实现
 * @copyright Copyright (c) 2026
 */

#include "mrtk_mail_box.h"
#if (MRTK_USING_MAILBOX == 1)

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

#define MRTK_MAIL_SIZE sizeof(mrtk_base_t)

/* ==============================================================================
 * 邮箱管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_mb_init(mrtk_mb_t *mb, mrtk_void_t *msg_pool, mrtk_u32_t size,
                        const mrtk_char_t *name, mrtk_u8_t flag)
{
    /* 参数检查 */
    if (mb == MRTK_NULL || msg_pool == MRTK_NULL || name == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    _mrtk_ipc_obj_init(&mb->ipc_obj, MRTK_OBJECT_TYPE_STATIC | MRTK_OBJ_TYPE_MAIL, flag, name);

    mb->msg_pool     = (mrtk_u32_t *) msg_pool;
    mb->max_mail_cnt = MRTK_ALIGN_DOWN(size, MRTK_ALIGN_SIZE) / MRTK_MAIL_SIZE;
    mb->cur_mail_cnt = 0;
    mb->in_offset    = 0;
    mb->out_offset   = 0;
    _mrtk_list_init(&mb->suspend_sender);
    return MRTK_EOK;
}

mrtk_err_t mrtk_mb_detach(mrtk_mb_t *mb)
{
    if (mb == MRTK_NULL || MRTK_OBJ_IS_DYNAMIC(mb->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    /* 唤醒所有等待发送的任务 */
    if (_mrtk_ipc_resume_all(&mb->suspend_sender, MRTK_EDELETED) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }
    if (_mrtk_ipc_obj_delete(mb) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_mb_t *mrtk_mb_create(mrtk_u32_t size, const mrtk_char_t *name, mrtk_u8_t flag)
{
    if (name == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_mb_t *mb = (mrtk_mb_t *) mrtk_malloc(sizeof(mrtk_mb_t));
    if (mb == MRTK_NULL) {
        return MRTK_NULL;
    }

    mrtk_u32_t  aligned_size = MRTK_ALIGN_UP(size, MRTK_ALIGN_SIZE);
    mrtk_u32_t *msg_pool     = (mrtk_u32_t *) mrtk_malloc(aligned_size);
    if (msg_pool == MRTK_NULL) {
        mrtk_free(mb);
        return MRTK_NULL;
    }

    /* 初始化邮箱 */
    mrtk_err_t ret = mrtk_mb_init(mb, msg_pool, aligned_size, name, flag);
    if (ret != MRTK_EOK) {
        mrtk_free(mb);
        mrtk_free(msg_pool);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配（直接使用位或操作，避免宏展开问题） */
    mb->ipc_obj.obj.type |= MRTK_OBJECT_TYPE_DYNAMIC;

    return mb;
}

mrtk_err_t mrtk_mb_delete(mrtk_mb_t *mb)
{
    if (mb == MRTK_NULL || MRTK_OBJ_IS_STATIC(mb->ipc_obj.obj.type)) {
        return MRTK_EINVAL;
    }

    mrtk_base_t level         = mrtk_hw_interrupt_disable();
    mrtk_bool_t need_schedule = MRTK_FALSE;

    /* 唤醒所有等待发送的任务 */
    if (_mrtk_ipc_resume_all(&mb->suspend_sender, MRTK_EDELETED) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }
    if (_mrtk_ipc_obj_delete(mb) == MRTK_TRUE) {
        need_schedule = MRTK_TRUE;
    }

    mrtk_hw_interrupt_enable(level);

    mrtk_free(mb->msg_pool);
    mrtk_free(mb);
    /* 临界区外统一触发调度 */
    if (need_schedule) {
        mrtk_schedule();
    }

    return MRTK_EOK;
}

mrtk_err_t mrtk_mb_send_wait(mrtk_mb_t *mb, mrtk_u32_t *mail, mrtk_s32_t timeout)
{
    if (mb == MRTK_NULL || mail == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 中断上下文检查 */
    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        return MRTK_E_IN_ISR;
    }

    mrtk_base_t  level    = mrtk_hw_interrupt_disable();
    mrtk_task_t *cur_task = mrtk_task_self();

    /* 邮箱满，等待空闲位置 */
    while (mb->max_mail_cnt == mb->cur_mail_cnt) {
        /* 非阻塞模式 */
        if (timeout == 0) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EFULL;
        }

        cur_task->last_error = MRTK_EOK;

        /* 处理超时 */
        if (timeout > 0) {
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
            mrtk_timer_start(&cur_task->timer);
        }

        /* 挂起任务 */
        _mrtk_ipc_suspend_one(&mb->suspend_sender, mb->ipc_obj.flag, cur_task);
        mrtk_hw_interrupt_enable(level);
        mrtk_schedule();
        level = mrtk_hw_interrupt_disable();

        /* 检查唤醒原因 */
        if (cur_task->last_error != MRTK_EOK) {
            mrtk_hw_interrupt_enable(level);
            return cur_task->last_error;
        }
    }

    /* 邮件写入环形缓冲区 */
    mb->msg_pool[mb->in_offset] = *mail;
    mb->in_offset               = (mb->in_offset + 1) % mb->max_mail_cnt;
    mb->cur_mail_cnt++;

    mrtk_bool_t need_schedule = MRTK_FALSE;
    /* 如果有接收者等待，唤醒接收者 */
    if (!_mrtk_list_is_empty(&mb->ipc_obj.suspend_taker)) {
        if (_mrtk_ipc_resume_one(&mb->ipc_obj.suspend_taker, MRTK_EOK) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_err_t mrtk_mb_recv(mrtk_mb_t *mb, mrtk_u32_t *mail, mrtk_s32_t timeout)
{
    if (mb == MRTK_NULL || mail == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    /* 中断上下文检查 */
    if (timeout != 0 && mrtk_irq_get_nest() != 0) {
        return MRTK_E_IN_ISR;
    }

    mrtk_base_t  level    = mrtk_hw_interrupt_disable();
    mrtk_task_t *cur_task = mrtk_task_self();

    /* 邮箱空，等待邮件 */
    while (mb->cur_mail_cnt == 0) {
        /* 非阻塞模式 */
        if (timeout == 0) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_EEMPTY;
        }

        cur_task->last_error = MRTK_EOK;

        /* 处理超时 */
        if (timeout > 0) {
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &timeout);
            mrtk_timer_start(&cur_task->timer);
        }

        /* 挂起任务 */
        _mrtk_ipc_suspend_one(&mb->ipc_obj.suspend_taker, mb->ipc_obj.flag, cur_task);
        mrtk_hw_interrupt_enable(level);
        mrtk_schedule();
        level = mrtk_hw_interrupt_disable();

        /* 检查唤醒原因 */
        if (cur_task->last_error != MRTK_EOK) {
            mrtk_hw_interrupt_enable(level);
            return cur_task->last_error;
        }
    }

    /* 从环形缓冲区读取邮件 */
    *mail          = mb->msg_pool[mb->out_offset];
    mb->out_offset = (mb->out_offset + 1) % mb->max_mail_cnt;
    --mb->cur_mail_cnt;

    /* 如果有发送者等待，唤醒发送者 */
    mrtk_bool_t need_schedule = MRTK_FALSE;
    if (!_mrtk_list_is_empty(&mb->suspend_sender)) {
        if (_mrtk_ipc_resume_one(&mb->suspend_sender, MRTK_EOK) == MRTK_TRUE) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
    return MRTK_EOK;
}

mrtk_err_t mrtk_mb_control(mrtk_mb_t *mb, mrtk_u32_t cmd, mrtk_void_t *args)
{
    if (mb == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    mrtk_u8_t   need_schedule = MRTK_FALSE;
    mrtk_base_t level         = mrtk_hw_interrupt_disable();

    switch (cmd) {
    case MRTK_MB_CMD_RESET:
        /* 重置邮箱状态 */
        mb->cur_mail_cnt = 0;
        mb->in_offset    = 0;
        mb->out_offset   = 0;

        /* 唤醒所有接收者，告知邮箱已重置 */
        need_schedule |= _mrtk_ipc_resume_all(&mb->ipc_obj.suspend_taker, MRTK_EDELETED);

        /* 唤醒所有发送者，告知邮箱已重置 */
        need_schedule |= _mrtk_ipc_resume_all(&mb->suspend_sender, MRTK_EDELETED);

        mrtk_hw_interrupt_enable(level);

        /* 触发调度 */
        if (need_schedule) {
            mrtk_schedule();
        }
        return MRTK_EOK;

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
 * @brief 邮箱对象信息导出
 * @details 打印邮箱的当前状态，包括容量、使用情况、等待队列等
 */
mrtk_void_t mrtk_mb_dump(mrtk_mb_t *mb)
{
    /* 防御性编程：检查空指针 */
    if (mb == MRTK_NULL) {
        mrtk_printf("[MailBox Dump] Error: mailbox pointer is MRTK_NULL\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 读取关键信息 */
    const mrtk_char_t *name        = mb->ipc_obj.obj.name;
    mrtk_u32_t         max_cnt     = mb->max_mail_cnt;
    mrtk_u32_t         cur_cnt     = mb->cur_mail_cnt;
    mrtk_u32_t         in_offset   = mb->in_offset;
    mrtk_u32_t         out_offset  = mb->out_offset;
    mrtk_u32_t         suspend_cnt = _mrtk_list_len(&mb->ipc_obj.suspend_taker);
    mrtk_u32_t         sender_cnt  = _mrtk_list_len(&mb->suspend_sender);
    mrtk_u8_t          is_dynamic  = MRTK_OBJ_IS_DYNAMIC(mb->ipc_obj.obj.type);
    float              usage_ratio = (max_cnt > 0) ? ((cur_cnt * 100.0f) / max_cnt) : 0.0f;

    mrtk_hw_interrupt_enable(level);

    /* 格式化打印邮箱信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[MailBox Dump] %s\r\n", name);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("  Address        : 0x%p\r\n", (void *) mb);
    mrtk_printf("  Type           : %s\r\n", is_dynamic ? "DYNAMIC" : "STATIC");
    mrtk_printf("  MsgPool Addr   : 0x%p\r\n", (void *) mb->msg_pool);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("  Max Mail Count : %u\r\n", (unsigned int) max_cnt);
    mrtk_printf("  Cur Mail Count : %u\r\n", (unsigned int) cur_cnt);
    mrtk_printf("  Usage Ratio    : %.1f%%\r\n", usage_ratio);
    mrtk_printf("  In Offset      : %u\r\n", (unsigned int) in_offset);
    mrtk_printf("  Out Offset     : %u\r\n", (unsigned int) out_offset);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");
    mrtk_printf("  Waiting Tasks  : %u (waiting to receive)\r\n", (unsigned int) suspend_cnt);
    mrtk_printf("  Waiting Tasks  : %u (waiting to send)\r\n", (unsigned int) sender_cnt);
    mrtk_printf("  Wake Strategy  : %s\r\n",
                (mb->ipc_obj.flag == MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO) ? "FIFO" : "PRIO");
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_MAILBOX == 1) */
