/**
 * @file mrtk_mail_box.h
 * @author leiyx
 * @brief 邮箱管理模块接口定义
 * @details 提供基于指针的消息传递功能，适用于传递少量数据或指针
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_MAIL_BOX_H__
#define __MRTK_MAIL_BOX_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 邮箱控制命令枚举
 * ============================================================================== */

/**
 * @brief 邮箱控制命令枚举
 * @details 用于 mrtk_mb_control() 函数的命令参数
 */
typedef enum {
    MRTK_MB_CMD_RESET = 0x01, /**< 重置邮箱，清空所有邮件（arg: MRTK_NULL） */
} mrtk_mb_cmd_t;

/* ==============================================================================
 * 邮箱控制块定义
 * ============================================================================== */

/**
 * @brief 邮箱控制块结构体
 * @details 采用环形缓冲区实现，存储固定长度的邮件（32位值）
 */
typedef struct mrtk_mb_def {
    mrtk_ipc_obj_t ipc_obj; /**< IPC 对象基类 */

    /* 邮箱池信息 */
    mrtk_u32_t *msg_pool;     /**< 邮箱池地址（环形缓冲区） */
    mrtk_u16_t  max_mail_cnt; /**< 邮箱容量（最大邮件数量） */

    /* 环形缓冲区状态 */
    mrtk_u16_t cur_mail_cnt; /**< 当前邮件数量 */
    mrtk_u16_t in_offset;    /**< 写入偏移量 */
    mrtk_u16_t out_offset;   /**< 读取偏移量 */

    /* 发送者阻塞队列 */
    mrtk_list_t suspend_sender; /**< 邮箱满时，等待发送的任务队列 */
} mrtk_mb_t;

/* ==============================================================================
 * 邮箱管理 API
 * ============================================================================== */

/**
 * @brief 邮箱静态初始化
 * @details 使用用户提供的邮箱池内存初始化邮箱
 * @param[out] mb       邮箱控制块指针
 * @param[in]  msg_pool 邮箱池地址
 * @param[in]  size     邮箱容量（邮件数量）
 * @param[in]  name     邮箱名称
 * @param[in]  flag     等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_init(mrtk_mb_t *mb, mrtk_void_t *msg_pool, mrtk_u32_t size, const mrtk_char_t *name,
                        mrtk_u8_t flag);

/**
 * @brief 邮箱静态脱离
 * @details 从系统对象管理中移除邮箱，唤醒所有等待任务
 * @param[in] mb 邮箱控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_detach(mrtk_mb_t *mb);

/**
 * @brief 邮箱动态创建
 * @details 从内存堆中分配邮箱控制块和邮箱池
 * @param[in] size 邮箱容量（邮件数量）
 * @param[in] name 邮箱名称
 * @param[in] flag 等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @return mrtk_mb_t* 成功返回邮箱指针，失败返回 MRTK_NULL
 */
mrtk_mb_t *mrtk_mb_create(mrtk_u32_t size, const mrtk_char_t *name, mrtk_u8_t flag);

/**
 * @brief 邮箱动态删除
 * @details 释放邮箱占用的所有内存，唤醒所有等待任务
 * @param[in] mb 邮箱控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_delete(mrtk_mb_t *mb);

/**
 * @brief 发送邮件（阻塞等待）
 * @details 向邮箱发送一封邮件（32位值），邮箱满则等待指定时间
 * @param[in] mb      邮箱控制块指针
 * @param[in] mail    要发送的邮件（32位值，通常为指针或整数）
 * @param[in] timeout 等待超时时间（单位为 Tick）
 *                    0 表示不阻塞，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK   成功发送
 * @retval MRTK_EFULL 邮箱满且超时
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_send_wait(mrtk_mb_t *mb, mrtk_u32_t *mail, mrtk_s32_t timeout);

/**
 * @brief 发送邮件（非阻塞）
 * @details 非阻塞方式向邮箱发送一封邮件，邮箱满则立即返回错误
 * @param[in] mb   邮箱控制块指针
 * @param[in] mail 要发送的邮件（32位值）
 * @retval MRTK_EOK   成功发送
 * @retval MRTK_EFULL 邮箱满
 * @retval MRTK_EINVAL 参数错误
 */
#define mrtk_mb_send(mb, mail) mrtk_mb_send_wait(mb, mail, 0)

/**
 * @brief 接收邮件
 * @details 从邮箱接收一封邮件，邮箱空则等待指定时间
 * @param[in]     mb      邮箱控制块指针
 * @param[out]    mail    存储接收到的邮件的指针
 * @param[in]     timeout 等待超时时间（单位为 Tick）
 *                      0 表示不阻塞，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK    成功接收
 * @retval MRTK_EEMPTY 邮箱空且超时
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_recv(mrtk_mb_t *mb, mrtk_u32_t *mail, mrtk_s32_t timeout);

/**
 * @brief 邮箱属性控制
 * @details 查询或修改邮箱属性
 * @param[in]     mb   邮箱控制块指针
 * @param[in]     cmd  控制命令（见 mrtk_mb_cmd_t 枚举）
 * @param[in,out] args 命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mb_control(mrtk_mb_t *mb, mrtk_u32_t cmd, mrtk_void_t *args);

#if (MRTK_DEBUG == 1)
/**
 * @brief 邮箱对象信息导出
 * @details 打印邮箱的当前状态，包括容量、使用情况、等待队列等
 * @param[in] mb 邮箱对象指针
 */
mrtk_void_t mrtk_mb_dump(mrtk_mb_t *mb);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_MAIL_BOX_H__ */
