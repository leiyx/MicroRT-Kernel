/**
 * @file mrtk_mem_heap.h
 * @author leiyx
 * @brief 堆内存管理模块接口定义
 * @details 提供变长内存块的动态分配和释放功能（类似标准 C 库的 malloc/free）
 * @note 使用空闲链表组织内存块，可能产生内存碎片化问题
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_MEM_HEAP_H__
#define __MRTK_MEM_HEAP_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 堆内存内部常量定义
 * ============================================================================== */

/* 静态堆缓冲区（内核管理的全局内存） */
extern mrtk_u8_t g_heap_buffer[MRTK_HEAP_SIZE];

/** 内存块头部大小（字节） */
#define MRTK_HEAP_HEADER_SIZE MRTK_ALIGN_UP(sizeof(mrtk_heap_mem_t), MRTK_ALIGN_SIZE)

/** 内存块数据部分的最小容量（字节） */
#define MRTK_HEAP_DATA_MIN_SIZE MRTK_HEAP_HEADER_SIZE

/** 内存块魔数（用于检测内存越界） */
#define MRTK_HEAP_MAGIC 0x1ea0

/* ==============================================================================
 * 堆内存块状态枚举
 * ============================================================================== */

/**
 * @brief 堆内存块状态枚举
 * @details 标识内存块的当前状态
 */
typedef enum {
    MRTK_HEAP_BLOCK_STATE_DUMMY = 0, /**< 哨兵块（堆边界标记） */
    MRTK_HEAP_BLOCK_STATE_FREE  = 1, /**< 空闲块（未分配） */
    MRTK_HEAP_BLOCK_STATE_USED  = 2, /**< 已分配块（正在使用） */
} mrtk_heap_block_state_t;

/* ==============================================================================
 * 堆内存块结构体定义
 * ============================================================================== */

/**
 * @brief 堆内存块结构体
 * @details 堆内存根据用户申请切分为不同大小的内存块，每个内存块都有一个头部信息
 * @note 使用相对偏移量（prev/next）而非绝对地址，便于内存搬移
 */
typedef struct mrtk_heap_mem_def {
    mrtk_u16_t  magic; /**< 魔数（用于内存越界检测） */
    mrtk_u16_t  state; /**< 内存块状态（见 mrtk_heap_block_state_t 枚举） */
    mrtk_size_t prev;  /**< 上一个内存块起始地址相对堆起始地址的偏移量 */
    mrtk_size_t next;  /**< 下一个内存块起始地址相对堆起始地址的偏移量 */
} mrtk_heap_mem_t;

/* ==============================================================================
 * 堆内存管理 API
 * ============================================================================== */

/**
 * @brief 初始化堆内存管理器
 * @details 将指定内存区域初始化为堆，构建空闲链表
 * @param[in] begin 堆起始地址
 * @param[in] end   堆结束地址（不包含）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误（begin >= end 或区域太小）
 */
mrtk_err_t mrtk_heap_init(mrtk_void_t *begin, mrtk_void_t *end);

/**
 * @brief 从堆中分配内存
 * @details 根据请求大小查找合适的空闲块，分配后可能产生碎片
 * @param[in] size 请求分配的内存大小（字节）
 * @return mrtk_void_t* 成功返回内存地址，失败返回 MRTK_NULL
 */
mrtk_void_t *mrtk_malloc(mrtk_size_t size);

/**
 * @brief 释放堆内存
 * @details 将内存块归还给堆，与相邻空闲块自动合并
 * @param[in] ptr 要释放的内存地址（必须由 mrtk_malloc 返回）
 */
mrtk_void_t mrtk_free(mrtk_void_t *ptr);

/* ==============================================================================
 * 对象状态导出 API (Object Dump API)
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出堆内存状态信息到控制台
 * @details 打印堆的起始地址、总大小、lfree 指针及前5个内存块状态
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_heap_dump(mrtk_void_t);

#endif /* (MRTK_DEBUG == 1) */

#ifdef UNIT_TESTING

#endif

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_MEM_HEAP_H__ */
