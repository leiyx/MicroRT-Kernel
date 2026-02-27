/**
 * @file mrtk_msg_queue.h
 * @author leiyx
 * @brief 消息队列管理模块接口定义
 * @details 提供变长消息传递功能，支持阻塞发送/接收和紧急消息
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_MSG_QUEUE_H__
#define __MRTK_MSG_QUEUE_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_list.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 消息队列控制命令枚举
 * ============================================================================== */

/**
 * @brief 消息队列控制命令枚举
 * @details 用于 mrtk_mq_control() 函数的命令参数
 */
typedef enum {
    MRTK_MQ_CMD_RESET          = 0x00, /**< 重置消息队列，清空所有消息 (arg: MRTK_NULL) */
    MRTK_MQ_CMD_GET_CUR_MSG_CNT = 0x01, /**< 获取当前消息数量 (arg: mrtk_u16_t *) */
    MRTK_MQ_CMD_GET_MAX_MSG_CNT = 0x02, /**< 获取最大消息数量 (arg: mrtk_u16_t *) */
} mrtk_mq_cmd_t;

/* ==============================================================================
 * 消息队列内部定义
 * ============================================================================== */

/** 消息头部大小（字节） */
#define MRTK_MQ_MSG_HEADER_SIZE sizeof(mrtk_mq_msg_header_t)

/**
 * @brief 消息队列消息头部结构体
 * @details 每个消息都有一个头部，用于构建消息链表
 */
typedef struct mrtk_mq_msg_header_def {
    struct mrtk_mq_msg_header_def *next; /**< 指向下一个消息节点 */
} mrtk_mq_msg_header_t;

/* ==============================================================================
 * 消息队列控制块定义
 * ============================================================================== */

/**
 * @brief 消息队列控制块结构体
 * @details 管理消息池和消息链表，支持多发送者-多接收者模式
 */
typedef struct mrtk_mq_def {
    mrtk_ipc_obj_t ipc_obj; /**< IPC 对象基类 */

    /* 消息池信息 */
    mrtk_void_t *msg_pool;     /**< 消息池起始地址 */
    mrtk_u32_t   max_msg_size; /**< 单条消息最大长度（字节） */
    mrtk_u16_t   max_msg_cnt;  /**< 最大消息数量 */
    mrtk_u16_t   cur_msg_cnt;  /**< 当前消息数量 */

    /* 消息链表 */
    mrtk_void_t *head_msg; /**< 消息链表头部（指向最早到达的消息） */
    mrtk_void_t *tail_msg; /**< 消息链表尾部（指向最后到达的消息） */
    mrtk_void_t *free_msg; /**< 空闲消息链表（指向第一个未被使用的内存块） */

    /* 发送者阻塞队列 */
    mrtk_list_t suspend_releaser; /**< 队列满时，等待发送的任务队列 */
} mrtk_mq_t;

/* ==============================================================================
 * 消息队列管理 API
 * ============================================================================== */

/**
 * @brief 消息队列静态初始化
 * @details 使用用户提供的消息池内存初始化消息队列
 * @param[in] mq        消息队列控制块指针
 * @param[in] name      消息队列名称
 * @param[in] msgpool   消息池起始地址
 * @param[in] msg_size  单条消息最大长度（字节）
 * @param[in] pool_size 消息池总大小（字节）
 * @param[in] flag      等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_init(mrtk_mq_t *mq, const mrtk_char_t *name, mrtk_void_t *msgpool,
                        mrtk_size_t msg_size, mrtk_size_t pool_size, mrtk_u8_t flag);

/**
 * @brief 消息队列静态脱离
 * @details 从系统对象管理中移除消息队列，唤醒所有等待任务
 * @param[in] mq 消息队列控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_detach(mrtk_mq_t *mq);

/**
 * @brief 消息队列动态创建
 * @details 从内存堆中分配消息队列控制块和消息池
 * @param[in] name     消息队列名称
 * @param[in] msg_size 单条消息最大长度（字节）
 * @param[in] max_msgs 最大消息数量
 * @param[in] flag     等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @return mrtk_mq_t* 成功返回消息队列指针，失败返回 MRTK_NULL
 */
mrtk_mq_t *mrtk_mq_create(const mrtk_char_t *name, mrtk_size_t msg_size, mrtk_size_t max_msgs,
                          mrtk_u8_t flag);

/**
 * @brief 消息队列动态删除
 * @details 释放消息队列占用的所有内存，唤醒所有等待任务
 * @param[in] mq 消息队列控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_delete(mrtk_mq_t *mq);

/**
 * @brief 发送消息（非阻塞）
 * @details 尝试向消息队列发送一条消息，队列满则立即返回错误
 * @param[in] mq     消息队列控制块指针
 * @param[in] buffer 消息内容缓冲区指针
 * @param[in] size   要发送的消息大小（字节）
 * @retval MRTK_EOK   成功发送
 * @retval MRTK_EFULL 队列满
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_send(mrtk_mq_t *mq, const mrtk_void_t *buffer, mrtk_size_t size);

/**
 * @brief 发送消息（阻塞等待）
 * @details 向消息队列发送一条消息，队列满则等待指定时间
 * @param[in] mq      消息队列控制块指针
 * @param[in] buffer  消息内容缓冲区指针
 * @param[in] size    要发送的消息大小（字节）
 * @param[in] timeout 等待超时时间（单位为 Tick）
 *                    0 表示不阻塞，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK    成功发送
 * @retval MRTK_EFULL  队列满且超时
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_send_wait(mrtk_mq_t *mq, const mrtk_void_t *buffer, mrtk_size_t size,
                             mrtk_s32_t timeout);

/**
 * @brief 发送紧急消息
 * @details 向消息队列发送紧急消息，插入到队列头部优先处理
 * @param[in] mq     消息队列控制块指针
 * @param[in] buffer 消息内容缓冲区指针
 * @param[in] size   要发送的消息大小（字节）
 * @retval MRTK_EOK   成功发送
 * @retval MRTK_EFULL 队列满
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_urgent(mrtk_mq_t *mq, const mrtk_void_t *buffer, mrtk_size_t size);

/**
 * @brief 接收消息
 * @details 从消息队列接收一条消息，队列为空则等待指定时间
 * @param[in]     mq      消息队列控制块指针
 * @param[out]    buffer  接收消息的缓冲区指针
 * @param[in]     size    缓冲区大小（字节）
 * @param[in]     timeout 等待超时时间（单位为 Tick）
 *                      0 表示不阻塞，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK    成功接收
 * @retval MRTK_EEMPTY 队列空且超时
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_recv(mrtk_mq_t *mq, mrtk_void_t *buffer, mrtk_size_t size, mrtk_s32_t timeout);

/**
 * @brief 消息队列属性控制
 * @details 查询或修改消息队列属性
 * @param[in]     mq  消息队列控制块指针
 * @param[in]     cmd 控制命令
 * @param[in,out] arg 命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mq_control(mrtk_mq_t *mq, int cmd, mrtk_void_t *arg);

#if (MRTK_DEBUG == 1)
/**
 * @brief 导出消息队列状态信息到控制台
 * @details 打印消息队列的容量、使用情况、等待队列等调试信息
 * @note 需要开启 MRTK_DEBUG 配置宏
 * @param[in] mq 消息队列控制块指针
 */
mrtk_void_t mrtk_mq_dump(mrtk_mq_t *mq);
#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_MSG_QUEUE_H__ */
