/**
 * @file mrtk_ringbuffer.h
 * @author leiyx
 * @brief 环形缓冲区（Ring Buffer）接口定义
 * @details 提供高效的循环 FIFO 缓冲区实现，适用于串口、网络等设备数据缓冲
 * @note 非线程安全，多线程环境需配合互斥机制使用
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_RINGBUFFER_H__
#define __MRTK_RINGBUFFER_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "mrtk_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 环形缓冲区控制块定义
 * ============================================================================== */

/**
 * @brief 环形缓冲区控制块结构体
 * @details 采用循环队列设计，始终空出 1 字节用于区分满和空状态
 * @note 缓冲区满时实际可用空间为 size - 1
 */
typedef struct mrtk_rb_def {
    mrtk_u8_t *buffer; /**< 缓冲区首地址 */
    mrtk_u32_t size;   /**< 缓冲区总大小（字节） */

    mrtk_u32_t read_idx;  /**< 读指针偏移：下一次读取数据的位置 */
    mrtk_u32_t write_idx; /**< 写指针偏移：下一次写入数据的位置 */
} mrtk_rb_t;

/* ==============================================================================
 * 环形缓冲区管理 API
 * ============================================================================== */

/**
 * @brief 初始化环形缓冲区
 * @param[out] rb 环形缓冲区控制块指针
 * @param[in] pool 缓冲区首地址
 * @param[in] size 缓冲区总大小
 */
mrtk_void_t mrtk_rb_init(mrtk_rb_t *rb, mrtk_u8_t *pool, mrtk_size_t size);

/**
 * @brief 从环形缓冲区读取数据
 * @param[in] rb 环形缓冲区控制块指针
 * @param[out] ptr 存储读取数据的目标缓冲区
 * @param[in] size 请求读取的数据长度
 * @return mrtk_size_t 实际读取的数据长度
 */
mrtk_size_t mrtk_rb_read(mrtk_rb_t *rb, mrtk_u8_t *ptr, mrtk_size_t size);

/**
 * @brief 向环形缓冲区写入数据
 * @param[in] rb 环形缓冲区控制块指针
 * @param[in] ptr 包含待写入数据的源缓冲区
 * @param[in] size 请求写入的数据长度
 * @return mrtk_size_t 实际写入的数据长度
 */
mrtk_size_t mrtk_rb_write(mrtk_rb_t *rb, const mrtk_u8_t *ptr, mrtk_size_t size);

/**
 * @brief 向环形缓冲区写入单个字符
 * @param[in] rb 环形缓冲区控制块指针
 * @param[in] ch 待写入的字符
 * @retval MRTK_EOK 成功
 * @retval MRTK_EFULL 环形缓冲区已满
 */
mrtk_err_t mrtk_rb_putc(mrtk_rb_t *rb, mrtk_char_t ch);

/**
 * @brief 从环形缓冲区读取单个字符
 * @param[in] rb 环形缓冲区控制块指针
 * @param[out] ch 存储读取的字符的指针
 * @retval MRTK_EOK 成功
 * @retval MRTK_EEMPTY 环形缓冲区为空
 */
mrtk_err_t mrtk_rb_getc(mrtk_rb_t *rb, mrtk_char_t *ch);

/**
 * @brief 判断环形缓冲区是否已满
 * @param[in] read_idx 下一个读取的位置
 * @param[in] write_idx 下一个写入的位置
 * @param[in] size 环形缓冲区大小
 * @retval MRTK_TRUE 环形缓冲区已满
 * @retval MRTK_FALSE 环形缓冲区未满
 */
static inline mrtk_bool_t mrtk_rb_is_full(mrtk_u32_t read_idx, mrtk_u32_t write_idx,
                                          mrtk_u32_t size)
{
    return ((write_idx + 1) % size == read_idx) ? MRTK_TRUE : MRTK_FALSE;
}

/**
 * @brief 判断环形缓冲区是否为空
 * @param[in] read_idx 下一个读取的位置
 * @param[in] write_idx 下一个写入的位置
 * @retval MRTK_TRUE 环形缓冲区为空
 * @retval MRTK_FALSE 环形缓冲区不为空
 */
static inline mrtk_bool_t mrtk_rb_is_empty(mrtk_u32_t read_idx, mrtk_u32_t write_idx)
{
    return (write_idx == read_idx) ? MRTK_TRUE : MRTK_FALSE;
}

/**
 * @brief 获取环形缓冲区中已存储的数据长度
 * @param[in] rb 环形缓冲区控制块指针
 * @return mrtk_size_t 当前缓冲区中已存储的数据长度
 */
static inline mrtk_size_t mrtk_rb_get_len(mrtk_rb_t *rb)
{
    if (mrtk_rb_is_empty(rb->read_idx, rb->write_idx)) {
        return 0;
    }
    return (rb->write_idx > rb->read_idx) ? (rb->write_idx - rb->read_idx)
                                          : (rb->size - rb->read_idx + rb->write_idx);
}

/**
 * @brief 获取环形缓冲区的剩余可用空间
 * @param[in] rb 环形缓冲区控制块指针
 * @return mrtk_size_t 缓冲区剩余可用空间
 */
static inline mrtk_size_t mrtk_rb_get_free(mrtk_rb_t *rb)
{
    return rb->size - 1 - mrtk_rb_get_len(rb);
}

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_RINGBUFFER_H__ */