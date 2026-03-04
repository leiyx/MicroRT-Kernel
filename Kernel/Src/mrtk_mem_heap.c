/**
 * @file mrtk_mem_heap.c
 * @author leiyx
 * @brief 堆内存管理模块实现
 * @details 提供变长内存块的动态分配和释放功能，支持相邻空闲块自动合并
 * @note 使用空闲链表组织内存块，可能产生内存碎片化问题
 * @copyright Copyright (c) 2026
 */

/* 必须首先包含配置文件以读取配置宏 */
#include "mrtk_config_internal.h"

#if (MRTK_USING_MEM_HEAP == 1)

#include "cpu_port.h"
#include "mrtk_errno.h"
#include "mrtk_mem_heap.h"
#include "mrtk_schedule.h"
#include "mrtk_typedef.h"

#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif
#include "mrtk_utils.h"

/* ==============================================================================
 * 全局变量定义
 * ============================================================================== */

// mrtk_u8_t g_heap_buffer[MRTK_HEAP_SIZE];
__attribute__((section(".ccmram"))) mrtk_u8_t g_heap_buffer[MRTK_HEAP_SIZE];



/** 堆的起始地址（对齐后） */
mrtk_u8_t *heap_base_addr = MRTK_NULL;

/** 堆的大小，单位字节（对齐后） */
mrtk_size_t heap_size = 0;

/** 永远指向堆中地址最低的空闲内存块 */
mrtk_heap_mem_t *lfree = MRTK_NULL;

/* ==============================================================================
 * 堆内存管理 API 实现
 * ============================================================================== */

mrtk_err_t mrtk_heap_init(mrtk_void_t *begin, mrtk_void_t *end)
{
    /* 堆起始地址向上对齐 */
    mrtk_u8_t *begin_addr = (mrtk_u8_t *) MRTK_ALIGN_UP((mrtk_ptr_t) begin, MRTK_ALIGN_SIZE);
    /* 堆结束地址向下对齐 */
    mrtk_u8_t *end_addr = (mrtk_u8_t *) MRTK_ALIGN_DOWN((mrtk_ptr_t) end, MRTK_ALIGN_SIZE);

    /* 堆的空间存不下起始哨兵和结束哨兵和一个内存块，报错返回 */
    if (end_addr - begin_addr < 3 * MRTK_HEAP_HEADER_SIZE + MRTK_HEAP_DATA_MIN_SIZE) {
        return MRTK_ERROR;
    }

    /* mrtk_heap_init 在系统启动阶段中调度器初始化之前调用 */

    heap_base_addr = begin_addr;
    heap_size      = end_addr - begin_addr;

    /* 起始哨兵初始化 */
    mrtk_heap_mem_t *start_dummy = (mrtk_heap_mem_t *) begin_addr;
    start_dummy->magic           = MRTK_HEAP_MAGIC;
    start_dummy->state           = MRTK_HEAP_BLOCK_STATE_DUMMY;
    start_dummy->next            = MRTK_HEAP_HEADER_SIZE;
    start_dummy->prev            = 0;

    /* 中间空闲块初始化 */
    mrtk_heap_mem_t *free_block = (mrtk_heap_mem_t *) (begin_addr + start_dummy->next);
    free_block->state           = MRTK_HEAP_BLOCK_STATE_FREE;
    free_block->magic           = MRTK_HEAP_MAGIC;
    free_block->next            = heap_size - MRTK_HEAP_HEADER_SIZE;
    free_block->prev            = 0;

    lfree = free_block;

    /* 结束哨兵初始化 */
    mrtk_heap_mem_t *end_dummy = (mrtk_heap_mem_t *) (begin_addr + free_block->next);
    end_dummy->magic           = MRTK_HEAP_MAGIC;
    end_dummy->state           = MRTK_HEAP_BLOCK_STATE_DUMMY;
    end_dummy->next            = heap_size;
    end_dummy->prev            = (mrtk_u8_t *) free_block - begin_addr;

    return MRTK_EOK;
}

mrtk_void_t *mrtk_malloc(mrtk_size_t size)
{
    /* 用户申请的大小向上对齐后加上内存头大小即为内核需要分配的大小 */
    mrtk_size_t needed_size = MRTK_ALIGN_UP(size, MRTK_ALIGN_SIZE) + MRTK_HEAP_HEADER_SIZE;

    mrtk_schedule_lock();

    mrtk_heap_mem_t *cur_free = lfree;

    for (; (mrtk_u8_t *) cur_free - heap_base_addr < heap_size;
         cur_free = (mrtk_heap_mem_t *) (heap_base_addr + cur_free->next)) {
        /* cur_free_size 为空闲内存块大小（内存头 + 数据部分） */
        mrtk_size_t cur_free_size =
            (mrtk_ptr_t) (heap_base_addr + cur_free->next) - (mrtk_ptr_t) cur_free;

        /* cur_free 不满足要求，找下个空闲块 */
        if (cur_free->state != MRTK_HEAP_BLOCK_STATE_FREE || cur_free_size < needed_size) {
            continue;
        }

        /* cur_free 满足要求 */
        if (cur_free_size - needed_size < MRTK_HEAP_HEADER_SIZE + MRTK_HEAP_DATA_MIN_SIZE) {
            /* 如果该空闲块剩余空间容纳不下一个内存头和堆内存块最小数据容量，
             * 则 cur_free 不切分，这点空间直接送了作为内部碎片 */
            cur_free->magic = MRTK_HEAP_MAGIC;
            cur_free->state = MRTK_HEAP_BLOCK_STATE_USED;
        } else {
            /* cur_free 需要切分 */
            mrtk_heap_mem_t *next_block =
                (mrtk_heap_mem_t *) ((mrtk_u8_t *) cur_free + needed_size);
            mrtk_heap_mem_t *nnext_block = (mrtk_heap_mem_t *) (heap_base_addr + cur_free->next);

            next_block->state = MRTK_HEAP_BLOCK_STATE_FREE;
            next_block->magic = MRTK_HEAP_MAGIC;
            next_block->next  = cur_free->next;
            next_block->prev  = (mrtk_u8_t *) cur_free - heap_base_addr;
            nnext_block->prev = (mrtk_u8_t *) next_block - heap_base_addr;

            cur_free->magic = MRTK_HEAP_MAGIC;
            cur_free->state = MRTK_HEAP_BLOCK_STATE_USED;
            cur_free->next  = (mrtk_u8_t *) next_block - heap_base_addr;
        }

        /* 更新 lfree */
        while (cur_free == lfree && lfree->state == MRTK_HEAP_BLOCK_STATE_USED) {
            lfree = (mrtk_heap_mem_t *) (heap_base_addr + lfree->next);
            /* 如果 lfree 的 next 已经是堆外了，说明满堆了 */
            if ((mrtk_u8_t *) lfree - heap_base_addr >= heap_size) {
                break;
            }
        }

        mrtk_schedule_unlock();
        return (mrtk_u8_t *) cur_free + MRTK_HEAP_HEADER_SIZE;
    }
    mrtk_schedule_unlock();

    return MRTK_NULL;
}

mrtk_void_t mrtk_free(mrtk_void_t *ptr)
{
    if (ptr == MRTK_NULL) {
        return;
    }

    mrtk_schedule_lock();

    mrtk_heap_mem_t *block = (mrtk_heap_mem_t *) ((mrtk_u8_t *) ptr - MRTK_HEAP_HEADER_SIZE);

    /* 异常情况校验：魔数值、内存块状态、内存块范围合法性 */
    if ((mrtk_u8_t *) block - heap_base_addr >= heap_size || block->magic != MRTK_HEAP_MAGIC ||
        block->state != MRTK_HEAP_BLOCK_STATE_USED) {
        mrtk_schedule_unlock();
        return;
    }

    block->state = MRTK_HEAP_BLOCK_STATE_FREE;

    mrtk_heap_mem_t *prev_block = (mrtk_heap_mem_t *) (heap_base_addr + block->prev);
    mrtk_heap_mem_t *next_block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);

    /* 是否需要向前合并 */
    if (prev_block->state == MRTK_HEAP_BLOCK_STATE_FREE) {
        prev_block->next = block->next;
        next_block->prev = block->prev;
        block            = prev_block;
    }

    /* 是否需要向后合并 */
    if (next_block->state == MRTK_HEAP_BLOCK_STATE_FREE) {
        block->next = next_block->next;
        /* 原 next_block 需要合并入 block，所以这里需要更新 next_block 的值来更新其 prev 指针 */
        next_block       = (mrtk_heap_mem_t *) (heap_base_addr + next_block->next);
        next_block->prev = (mrtk_u8_t *) block - heap_base_addr;
    }

    /* 维护 lfree */
    if (lfree > block) {
        lfree = block;
    }

    mrtk_schedule_unlock();
}

#if (MRTK_DEBUG == 1)

/* 堆内存块状态字符串映射表 */
static const mrtk_char_t *g_heap_block_state_str[] = {
    "DUMMY", /**< MRTK_HEAP_BLOCK_STATE_DUMMY */
    "FREE",  /**< MRTK_HEAP_BLOCK_STATE_FREE */
    "USED",  /**< MRTK_HEAP_BLOCK_STATE_USED */
};

/**
 * @brief 导出堆内存状态信息到控制台
 * @details 打印堆的起始地址、总大小、lfree 指针及前5个内存块状态
 * @note 内部 API，请勿在应用代码中直接调用
 */
mrtk_void_t mrtk_heap_dump(mrtk_void_t)
{
    /* 防御性编程：检查堆是否已初始化 */
    if (heap_base_addr == MRTK_NULL) {
        mrtk_printf("Dump Error: Heap not initialized\r\n");
        return;
    }

    mrtk_base_t level = mrtk_hw_interrupt_disable();

    /* 输出堆基本信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Heap Object Dump]\r\n");
    mrtk_printf("  Type        : HEAP\r\n");
    mrtk_printf("  StartAddr   : 0x%p\r\n", heap_base_addr);
    mrtk_printf("  EndAddr     : 0x%p\r\n", heap_base_addr + heap_size);
    mrtk_printf("  TotalSize   : %u bytes\r\n", heap_size);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出 lfree 指针信息 */
    mrtk_printf("  LFreePtr    : 0x%p (offset: %u)\r\n", lfree,
                (mrtk_u32_t) ((mrtk_u8_t *) lfree - heap_base_addr));
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 遍历并打印前 5 个内存块状态（防止刷屏） */
    mrtk_printf("  Block List  (First 5 blocks):\r\n");
    mrtk_heap_mem_t *block       = (mrtk_heap_mem_t *) heap_base_addr;
    mrtk_u32_t       block_count = 0;
    mrtk_size_t      total_free  = 0;
    mrtk_size_t      total_used  = 0;

    while ((mrtk_u8_t *) block - heap_base_addr < heap_size && block_count < 5) {
        /* 获取块状态字符串 */
        mrtk_char_t *state_str = (mrtk_char_t *) g_heap_block_state_str[block->state];

        /* 计算块大小（不包含头部） */
        mrtk_size_t block_size = (mrtk_u8_t *) (heap_base_addr + block->next) -
                                 (mrtk_u8_t *) block - MRTK_HEAP_HEADER_SIZE;

        /* 统计内存使用情况 */
        if (block->state == MRTK_HEAP_BLOCK_STATE_FREE) {
            total_free += block_size;
        } else if (block->state == MRTK_HEAP_BLOCK_STATE_USED) {
            total_used += block_size;
        }

        /* 输出块信息 */
        mrtk_printf("    [%u] Offset: %-6u  State: %-6s  Size: %-6u bytes  Magic: 0x%04X\r\n",
                    block_count, (mrtk_u32_t) ((mrtk_u8_t *) block - heap_base_addr), state_str,
                    block_size, block->magic);

        /* 移动到下一个块 */
        block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);
        block_count++;
    }

    /* 如果块数不足 5 个，说明遍历完了 */
    if ((mrtk_u8_t *) block - heap_base_addr >= heap_size) {
        mrtk_printf("    (Total blocks: %u)\r\n", block_count);
    } else {
        mrtk_printf("    ... (more blocks hidden, use debugger for full view)\r\n");
    }

    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出内存使用统计 */
    float usage_ratio = (total_used * 100.0f) / (total_used + total_free);
    mrtk_printf("  Memory Stat :\r\n");
    mrtk_printf("    Total Free : %u bytes\r\n", total_free);
    mrtk_printf("    Total Used : %u bytes\r\n", total_used);
    mrtk_printf("    Usage      : %.1f%% (first 5 blocks only)\r\n", usage_ratio);
    mrtk_printf(
        "================================================================================\r\n");

    mrtk_hw_interrupt_enable(level);
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_MEM_HEAP == 1) */
