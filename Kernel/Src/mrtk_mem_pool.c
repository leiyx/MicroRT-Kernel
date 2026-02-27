/**
 * @file mrtk_mem_pool.c
 * @author leiyx
 * @brief 内存池管理模块实现
 * @details 提供固定大小内存块的动态分配和释放功能，无内存碎片化问题
 * @copyright Copyright (c) 2026
 */

/* 必须首先包含配置文件以读取配置宏 */
#include "mrtk_config_internal.h"

#if (MRTK_USING_MEM_POOL == 1)

#include "cpu_port.h"
#include "mrtk_errno.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_mem_pool.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"
#include "mrtk_typedef.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

/* ==============================================================================
 * 内存池管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_mp_init(mrtk_mem_pool_t *mp, const mrtk_char_t *name, mrtk_void_t *start_addr,
                        mrtk_size_t size, mrtk_size_t block_size)
{
    _mrtk_obj_init(&(mp->obj), MRTK_OBJ_TYPE_MEM_POOL | MRTK_OBJECT_TYPE_STATIC, 0, name);

    mp->start_addr = start_addr;
    mp->size       = size;

    /* 对起始地址和块大小进行对齐处理 */
    mrtk_ptr_t aligned_start_addr = MRTK_ALIGN_UP((mrtk_ptr_t) start_addr, MRTK_ALIGN_SIZE);
    mp->block_size                = MRTK_ALIGN_UP(block_size, MRTK_ALIGN_SIZE);

    /* 计算总块数 */
    mp->total_block_count = (size - (aligned_start_addr - (mrtk_ptr_t) start_addr)) /
                            (mp->block_size + MRTK_POOL_BLOCK_HEADER_SIZE);
    mp->free_block_count = mp->total_block_count;
    if (mp->total_block_count == 0) {
        return MRTK_ERROR;
    }

    /* 设置空闲链表头 */
    mp->free_block_list = (mrtk_void_t *) (aligned_start_addr + MRTK_POOL_BLOCK_HEADER_SIZE);

    _mrtk_list_init(&mp->suspend_tasks_dummy);

    /* 初始化空闲块链表 */
    mrtk_u8_t  *block_data = (mrtk_u8_t *) aligned_start_addr + MRTK_POOL_BLOCK_HEADER_SIZE;
    mrtk_size_t offset;
    for (offset = 0; offset < mp->total_block_count - 1; ++offset) {
        mrtk_u8_t *next = block_data + mp->block_size + MRTK_POOL_BLOCK_HEADER_SIZE;
        /* 链表后继指针指向下个内存块数据域起始位置 */
        MRTK_POOL_SET_HEADER(block_data, next);
        block_data = next;
    }
    /* 尾节点后继指针置空 */
    MRTK_POOL_SET_HEADER(block_data, MRTK_NULL);

    return MRTK_EOK;
}

mrtk_err_t mrtk_mp_detach(mrtk_mem_pool_t *mp)
{
    mrtk_task_t *task;
    mrtk_task_t *next;
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 唤醒所有阻塞任务 */
    MRTK_LIST_FOR_EACH_SAFE(task, next, &mp->suspend_tasks_dummy, mrtk_tcb_t, sched_node)
    {
        task->last_error = MRTK_EDELETED;
        _mrtk_list_remove(&task->sched_node);
        _mrtk_schedule_insert_task(task);
        task->state = MRTK_TASK_STAT_READY;
#if (MRTK_USING_TIMER == 1)
        mrtk_timer_stop(&task->timer);
#endif
    }

    _mrtk_obj_delete(mp);

    mrtk_hw_interrupt_enable(level);

    mrtk_schedule();

    return MRTK_EOK;
}

mrtk_mem_pool_t *mrtk_mp_create(const mrtk_char_t *name, mrtk_size_t block_size,
                                mrtk_size_t block_count)
{
    block_size = MRTK_ALIGN_UP(block_size, MRTK_ALIGN_SIZE);
    /* 申请内存池控制块 */
    mrtk_mem_pool_t *mp = (mrtk_mem_pool_t *) mrtk_malloc(sizeof(mrtk_mem_pool_t));
    if (mp == MRTK_NULL) {
        return MRTK_NULL;
    }

    /* 申请内存池空间 */
    mrtk_size_t size = (block_size + MRTK_POOL_BLOCK_HEADER_SIZE) * block_count;

    mrtk_void_t *pool_buffer = mrtk_malloc(size);
    if (pool_buffer == MRTK_NULL) {
        mrtk_free(mp);
        return MRTK_NULL;
    }

    if (mrtk_mp_init(mp, name, pool_buffer, size, block_size) != MRTK_EOK) {
        mrtk_free(mp);
        mrtk_free(pool_buffer);
        return MRTK_NULL;
    }

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(mp->obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return mp;
}

mrtk_err_t mrtk_mp_destroy(mrtk_mem_pool_t *mp)
{
    mrtk_task_t *task;
    mrtk_task_t *next;
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 唤醒所有阻塞任务 */
    MRTK_LIST_FOR_EACH_SAFE(task, next, &mp->suspend_tasks_dummy, mrtk_tcb_t, sched_node)
    {
        task->last_error = MRTK_EDELETED;
        _mrtk_list_remove(&task->sched_node);
        _mrtk_schedule_insert_task(task);
        task->state = MRTK_TASK_STAT_READY;
#if (MRTK_USING_TIMER == 1)
        mrtk_timer_stop(&task->timer);
#endif
    }

    _mrtk_obj_delete(mp);

    mrtk_hw_interrupt_enable(level);

    mrtk_free(mp->start_addr);
    mrtk_free(mp);

    mrtk_schedule();

    return MRTK_EOK;
}

mrtk_void_t *mrtk_mp_alloc(mrtk_mem_pool_t *mp, mrtk_u32_t time)
{
    mrtk_u8_t   *block_data = MRTK_NULL;
    mrtk_ubase_t level      = mrtk_hw_interrupt_disable();

    while (mp->free_block_count == 0) {
        /* 非阻塞方式 */
        if (time == 0) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_NULL;
        }

        /* 挂起当前任务 */
        mrtk_task_t *cur_task = mrtk_task_self();

        /* FIFO 插入阻塞队列 */
        _mrtk_schedule_remove_task(cur_task);
        cur_task->state = MRTK_TASK_STAT_SUSPEND;
        _mrtk_list_insert_before(&mp->suspend_tasks_dummy, &cur_task->sched_node);

        mrtk_hw_interrupt_enable(level);

#if (MRTK_USING_TIMER == 1)
        if (time != MRTK_WAITING_FOREVER) {
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_TIME, &time);
            mrtk_timer_control(&cur_task->timer, MRTK_TIMER_CMD_SET_ONESHOT, MRTK_NULL);
            mrtk_timer_start(&cur_task->timer);
        }
#endif

        mrtk_schedule();

        /* 唤醒后重新关中断 */
        level = mrtk_hw_interrupt_disable();

        /* 检查唤醒原因：如果是超时或被 delete，则退出 */
        if (cur_task->last_error != MRTK_EOK) {
            mrtk_hw_interrupt_enable(level);
            return MRTK_NULL;
        }
    }

    /* 此时一定有内存块可用 */
    block_data = mp->free_block_list;

    /* 维护 free_block_list、free_block_count */
    mp->free_block_list = (mrtk_u8_t *) MRTK_POOL_GET_HEADER(block_data);
    MRTK_POOL_SET_HEADER(block_data, mp);
    --mp->free_block_count;

    mrtk_hw_interrupt_enable(level);

    return block_data;
}

mrtk_void_t mrtk_mp_free(mrtk_void_t *block_data)
{
    mrtk_bool_t  need_schedule = MRTK_FALSE;
    mrtk_ubase_t level         = mrtk_hw_interrupt_disable();

    mrtk_mem_pool_t *mp = (mrtk_mem_pool_t *) MRTK_POOL_GET_HEADER(block_data);

    /* 头插法插入空闲链表 */
    MRTK_POOL_SET_HEADER(block_data, mp->free_block_list);
    mp->free_block_list = (mrtk_u8_t *) block_data;
    ++mp->free_block_count;

    /* 若有阻塞的任务 */
    if (mp->suspend_tasks_dummy.next != &mp->suspend_tasks_dummy) {
        mrtk_task_t *task = _mrtk_list_entry(mp->suspend_tasks_dummy.next, mrtk_tcb_t, sched_node);

        task->last_error = MRTK_EOK;

#if (MRTK_USING_TIMER == 1)
        /* 强制停止等待定时器 */
        mrtk_timer_stop(&task->timer);
#endif

        _mrtk_list_remove(&task->sched_node);
        _mrtk_schedule_insert_task(task);
        task->state = MRTK_TASK_STAT_READY;

        if (mrtk_schedule_prio_ht(task, mrtk_task_self())) {
            need_schedule = MRTK_TRUE;
        }
    }

    mrtk_hw_interrupt_enable(level);
    if (need_schedule == MRTK_TRUE) {
        mrtk_schedule();
    }
}

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出内存池状态信息到控制台
 * @details 打印内存池的名称、块大小、使用率等调试信息
 * @note 内部 API，请勿在应用代码中直接调用
 * @param[in] mp 内存池控制块指针
 */
mrtk_void_t mrtk_mp_dump(mrtk_mem_pool_t *mp)
{
    /* 防御性编程：检查空指针 */
    if (mp == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    /* 计算使用率 */
    mrtk_size_t used_count  = mp->total_block_count - mp->free_block_count;
    /* 防御性编程：防止除0异常（测试环境中可能 total_block_count = 0） */
    float       usage_ratio = (mp->total_block_count > 0) ? ((used_count * 100.0f) / mp->total_block_count) : 0.0f;

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[MemPool Object Dump]\r\n");
    mrtk_printf("  Name              : %s\r\n", mp->obj.name);
    mrtk_printf("  Type              : MEM_POOL\r\n");
    mrtk_printf("  Address           : 0x%p\r\n", mp);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出内存池区域信息 */
    mrtk_printf("  StartAddr         : 0x%p\r\n", mp->start_addr);
    mrtk_printf("  TotalSize         : %u bytes\r\n", mp->size);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出块信息 */
    mrtk_printf("  BlockSize         : %u bytes\r\n", mp->block_size);
    mrtk_printf("  TotalBlocks       : %u\r\n", mp->total_block_count);
    mrtk_printf("  FreeBlocks        : %u\r\n", mp->free_block_count);
    mrtk_printf("  UsedBlocks        : %u\r\n", used_count);
    mrtk_printf("  Usage             : %.1f%%\r\n", usage_ratio);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出空闲链表信息 */
    mrtk_printf("  FreeListHead      : 0x%p\r\n", mp->free_block_list);
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_MEM_POOL == 1) */
