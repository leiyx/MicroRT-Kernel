/**
 * @file mrtk_mem_pool.h
 * @author leiyx
 * @brief 内存池管理模块接口定义
 * @details 提供固定大小内存块的动态分配和释放功能，无内存碎片化问题
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_MEM_POOL_H__
#define __MRTK_MEM_POOL_H__

#include "mrtk_config_internal.h"
#include "mrtk_list.h"
#include "mrtk_obj.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 内存池内部宏定义
 * ============================================================================== */

/** 内存块头部大小（存储指向下一个空闲块的指针） */
#define MRTK_POOL_BLOCK_HEADER_SIZE sizeof(mrtk_void_t *)

/**
 * @brief 根据内存块数据域首地址获取内存块头部值
 * @note 内存块布局：[头部指针][数据域]
 */
#define MRTK_POOL_GET_HEADER(block_data)                                                           \
    (*(mrtk_void_t **) ((mrtk_u8_t *) block_data - MRTK_POOL_BLOCK_HEADER_SIZE))

/**
 * @brief 根据内存块数据域首地址设置内存块头部值
 * @note 内存块布局：[头部指针][数据域]
 */
#define MRTK_POOL_SET_HEADER(block_data, val)                                                      \
    (*(mrtk_void_t **) ((mrtk_u8_t *) block_data - MRTK_POOL_BLOCK_HEADER_SIZE) = val)

/* ==============================================================================
 * 内存池控制块定义
 * ============================================================================== */

/**
 * @brief 内存池控制块结构体
 * @details 管理固定大小的内存块池，采用空闲链表组织
 * @note 每个内存块头部存储指向下一个空闲块的指针，数据域紧随其后
 */
typedef struct mrtk_mem_pool_def {
    mrtk_obj_t obj; /**< 内核对象基类 */

    /* 内存池区域信息 */
    mrtk_void_t *start_addr; /**< 内存池起始物理地址 */
    mrtk_size_t  size;       /**< 内存池总大小（字节） */

    /* 内存块信息 */
    mrtk_size_t block_size;        /**< 单个内存块数据块大小（字节） */
    mrtk_size_t total_block_count; /**< 内存块总数量 */
    mrtk_size_t free_block_count;  /**< 空闲内存块数量 */

    /* 空闲链表 */
    mrtk_u8_t *free_block_list; /**< 空闲块链表头（指向数据域首地址） */

    /* 阻塞任务队列 */
    mrtk_list_t suspend_tasks_dummy; /**< 内存不足时，等待分配的任务队列 */
} mrtk_mem_pool_t;

/* ==============================================================================
 * 内存池管理 API
 * ============================================================================== */

/**
 * @brief 内存池静态初始化
 * @details 将指定内存区域划分为固定大小的内存块，构建空闲链表
 * @param[out] mp         内存池控制块指针
 * @param[in]  name       内存池名称
 * @param[in]  start_addr 内存池起始地址
 * @param[in]  size       内存池总大小（字节）
 * @param[in]  block_size 单个内存块大小（字节）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误（大小不是 block_size 的整数倍等）
 */
mrtk_err_t mrtk_mp_init(mrtk_mem_pool_t *mp, const mrtk_char_t *name, mrtk_void_t *start_addr,
                        mrtk_size_t size, mrtk_size_t block_size);

/**
 * @brief 内存池静态脱离
 * @details 从系统对象管理中移除内存池，唤醒所有等待任务
 * @param[in] mp 内存池控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mp_detach(mrtk_mem_pool_t *mp);

/**
 * @brief 内存池动态创建
 * @details 从内存堆中分配内存池控制块和内存区域
 * @param[in] name        内存池名称
 * @param[in] block_size  单个内存块大小（字节）
 * @param[in] block_count 内存块数量
 * @return mrtk_mem_pool_t* 成功返回内存池控制块指针，失败返回 MRTK_NULL
 */
mrtk_mem_pool_t *mrtk_mp_create(const mrtk_char_t *name, mrtk_size_t block_size,
                                mrtk_size_t block_count);

/**
 * @brief 内存池动态删除
 * @details 释放内存池控制块和内存区域，唤醒所有等待任务
 * @param[in] mp 内存池控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mp_destroy(mrtk_mem_pool_t *mp);

/**
 * @brief 从内存池申请内存块
 * @details 从空闲链表中获取一个内存块，若无空闲块则根据 time 参数决定是否阻塞
 * @param[in] mp   内存池控制块指针
 * @param[in] time 等待超时时间（单位为 Tick）
 *                0 表示不阻塞，MRTK_WAIT_FOREVER 表示永久等待
 * @return mrtk_void_t* 成功返回内存块数据域首地址，失败返回 MRTK_NULL
 */
mrtk_void_t *mrtk_mp_alloc(mrtk_mem_pool_t *mp, mrtk_u32_t time);

/**
 * @brief 释放内存块回内存池
 * @details 将内存块加入空闲链表，若有等待任务则唤醒其中一个
 * @note 参数中没有 mrtk_mem_pool_t *mp，因为在 alloc 时，在块头部写入了所属 mp 的地址
 * @param[in] block_data 内存块数据域首地址
 */
mrtk_void_t mrtk_mp_free(mrtk_void_t *block_data);

/* ==============================================================================
 * 对象状态导出 API (Object Dump API)
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出内存池状态信息到控制台
 * @details 打印内存池的名称、块大小、使用率等调试信息
 * @param[in] mp 内存池控制块指针
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_mp_dump(mrtk_mem_pool_t *mp);

#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_MEM_POOL_H__ */