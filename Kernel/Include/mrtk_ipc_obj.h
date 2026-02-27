/**
 * @file mrtk_ipc_obj.h
 * @author leiyx
 * @brief IPC 对象基类定义
 * @details 定义信号量、互斥量、消息队列、邮箱等 IPC 对象的基类，提供统一的阻塞/唤醒机制
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_IPC_OBJ_H__
#define __MRTK_IPC_OBJ_H__

#include "mrtk_config_internal.h"
#include "mrtk_obj.h"
#include "mrtk_task.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * IPC 对象常量定义
 * ============================================================================== */

/** 无限等待时间（用于 IPC 阻塞函数） */
#define MRTK_IPC_WAIT_FOREVER -1
/** FIFO 唤醒策略（按等待顺序唤醒） */
#define MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 0x00
/** 优先级唤醒策略（按优先级唤醒） */
#define MRTK_IPC_FLAG_NOTIFY_POLICY_PRIO 0x01

/* ==============================================================================
 * IPC 对象基类定义
 * ============================================================================== */

/**
 * @brief IPC 对象基类结构体
 * @details 继承自 mrtk_obj_t，所有 IPC
 * 对象（信号量、互斥量、消息队列、邮箱）都应将此结构体作为第一个成员
 * @note 提供统一的任务阻塞队列和唤醒策略
 */
typedef struct mrtk_ipc_obj_def {
    mrtk_obj_t  obj;           /**< 内核对象基类 */
    mrtk_list_t suspend_taker; /**< 阻塞任务链表（等待此 IPC 资源的任务） */
    mrtk_u8_t   flag;          /**< 唤醒策略（FIFO/PRIO） */
} mrtk_ipc_obj_t;

/* ==============================================================================
 * IPC 对象基类 API
 * ============================================================================== */

/**
 * @brief IPC 对象基类构造函数
 * @details 初始化 IPC 对象基类，包括阻塞任务链表
 * @param[in] obj  IPC 对象基类指针
 * @param[in] type IPC 对象类型（见 mrtk_obj_type 枚举）
 * @param[in] flag 内存分配类型（MRTK_OBJECT_TYPE_STATIC 或 MRTK_OBJECT_TYPE_DYNAMIC）
 * @param[in] name IPC 对象名称
 */
mrtk_void_t _mrtk_ipc_obj_init(mrtk_void_t *obj, mrtk_u8_t type, mrtk_u8_t flag,
                              const mrtk_char_t *name);

/**
 * @brief IPC 对象基类析构函数
 * @details 唤醒所有阻塞在此 IPC 对象上的任务，并返回是否需要调度
 * @param[in] obj IPC 对象基类指针
 * @retval MRTK_TRUE 需要触发调度
 * @retval MRTK_FALSE 不需要触发调度
 */
mrtk_bool_t _mrtk_ipc_obj_delete(mrtk_void_t *obj);

/**
 * @brief 将任务阻塞到 IPC 对象的等待队列中
 * @details 根据 flag 参数决定插入策略（FIFO 或优先级）
 * @note 内部 API，请勿在应用代码中直接调用
 * @note 调用此函数前必须持有中断锁
 * @param[in] suspend_list 阻塞任务链表头指针
 * @param[in] flag          唤醒策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @param[in] task          要阻塞的任务指针
 */
mrtk_void_t _mrtk_ipc_suspend_one(mrtk_list_t *suspend_list, mrtk_u8_t flag, mrtk_task_t *task);

/**
 * @brief 从 IPC 等待队列中摘取第一个任务
 * @note 内部 API，请勿在应用代码中直接调用
 * @note 调用此函数前必须持有中断锁
 * @param[in] suspend_list 阻塞任务链表头指针
 * @param[in] error 任务错误码
 * @return mrtk_bool_t 是否需要触发调度（MRTK_TRUE：需要，MRTK_FALSE：不需要）
 */
mrtk_bool_t _mrtk_ipc_resume_one(mrtk_list_t *suspend_list, mrtk_err_t error);

/**
 * @brief 唤醒 IPC 对象等待队列中的所有任务
 * @note 内部 API，请勿在应用代码中直接调用
 * @note 调用此函数前必须持有中断锁
 * @param[in] suspend_list 阻塞任务链表头指针
 * @param[in] error 任务错误码
 * @return mrtk_bool_t 是否需要触发调度（MRTK_TRUE：需要，MRTK_FALSE：不需要）
 */
mrtk_bool_t _mrtk_ipc_resume_all(mrtk_list_t *suspend_list, mrtk_err_t error);

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_IPC_OBJ_H__ */