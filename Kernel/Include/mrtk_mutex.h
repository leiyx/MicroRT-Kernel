/**
 * @file mrtk_mutex.h
 * @author leiyx
 * @brief 互斥量管理模块接口定义
 * @details 提供互斥锁功能，支持优先级继承协议以防止优先级翻转
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_MUTEX_H__
#define __MRTK_MUTEX_H__

#include "mrtk_config_internal.h"
#include "mrtk_errno.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_list.h"
#include "mrtk_task.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 互斥量状态枚举
 * ============================================================================== */

/**
 * @brief 互斥量状态枚举
 */
typedef enum {
    MRTK_MUTEX_STATE_UNLOCKED = 0, /**< 未锁定状态 */
    MRTK_MUTEX_STATE_LOCKED   = 1, /**< 已锁定状态 */
} mrtk_mutex_state;

/* ==============================================================================
 * 互斥量对象定义
 * ============================================================================== */

/**
 * @brief 互斥量结构体
 * @details 继承自 IPC 对象基类，实现互斥锁和优先级继承机制
 */
typedef struct mrtk_mutex_def {
    mrtk_ipc_obj_t   ipc_obj;    /**< IPC 对象基类 */
    mrtk_u16_t       value;      /**< 互斥锁状态：0-未锁定，1-已锁定 */
    mrtk_u8_t        orig_prio;  /**< 持有者原始优先级（用于优先级继承） */
    mrtk_u8_t        nest;       /**< 递归嵌套计数（支持同任务多次获取） */
    mrtk_task_t     *owner_task; /**< 持有互斥锁的任务指针 */
    mrtk_list_node_t held_node;  /**< 挂入持有者任务的 held_mutex_list 的节点 */
} mrtk_mutex_t;

/* ==============================================================================
 * 互斥量控制命令定义
 * ============================================================================== */

/**
 * @brief 互斥量控制命令枚举
 * @details 用于 mrtk_mutex_control() 的 cmd 参数
 */
typedef enum {
    MRTK_MUTEX_CMD_GET_OWNER     = 0x00, /**< 获取持有者任务 (arg = mrtk_task_t **) */
    MRTK_MUTEX_CMD_GET_NEST      = 0x01, /**< 获取递归嵌套深度 (arg = mrtk_u8_t *) */
    MRTK_MUTEX_CMD_GET_ORIG_PRIO = 0x02, /**< 获取原始优先级 (arg = mrtk_u8_t *) */
} mrtk_mutex_cmd_t;

/* ==============================================================================
 * 互斥量管理 API
 * ============================================================================== */

/**
 * @brief 互斥量静态初始化
 * @details 初始化互斥量对象，设置唤醒策略
 * @param[out] mutex 互斥量对象指针
 * @param[in]  name  互斥量名称
 * @param[in]  flag  等待队列策略（FIFO 或 PRIO）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mutex_init(mrtk_mutex_t *mutex, const mrtk_char_t *name, mrtk_u8_t flag);

/**
 * @brief 互斥量静态脱离
 * @details 从系统对象管理中移除互斥量，唤醒所有等待任务
 * @param[in] mutex 互斥量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mutex_detach(mrtk_mutex_t *mutex);

/**
 * @brief 互斥量动态创建
 * @details 从内存堆中分配互斥量对象并初始化
 * @param[in] name 互斥量名称
 * @param[in] flag 等待队列策略（FIFO 或 PRIO）
 * @return mrtk_mutex_t* 成功返回互斥量指针，失败返回 MRTK_NULL
 */
mrtk_mutex_t *mrtk_mutex_create(const mrtk_char_t *name, mrtk_u8_t flag);

/**
 * @brief 互斥量动态删除
 * @details 释放互斥量对象占用的内存，唤醒所有等待任务
 * @param[in] mutex 互斥量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mutex_delete(mrtk_mutex_t *mutex);

/**
 * @brief 获取互斥量（阻塞模式）
 * @details 尝试获取互斥锁，若已被占用则根据优先级继承协议处理
 * @note 支持优先级继承协议以防止优先级翻转，支持递归锁（同任务可多次获取）
 * @param[in] mutex   互斥量对象指针
 * @param[in] timeout 等待超时时间（单位为 Tick）
 *                    0 表示立即返回，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK    成功获取
 * @retval MRTK_EFULL  锁已被占用且超时
 * @retval MRTK_EINVAL 参数错误
 * @retval MRTK_EDELETED 互斥量被删除
 */
mrtk_err_t mrtk_mutex_take(mrtk_mutex_t *mutex, mrtk_u32_t timeout);

/**
 * @brief 尝试获取互斥量（非阻塞模式）
 * @details 非阻塞方式尝试获取互斥锁，立即返回
 * @param[in] mutex 互斥量对象指针
 * @retval MRTK_EOK   成功获取
 * @retval MRTK_EFULL 锁已被占用
 * @retval MRTK_EINVAL 参数错误
 * @retval MRTK_EDELETED 互斥量被删除
 */
#define mrtk_mutex_trytake(mutex) mrtk_mutex_take(mutex, 0)

/**
 * @brief 释放互斥量
 * @details 释放互斥锁，如果有等待任务则唤醒其中一个
 * @note 仅限持有者任务释放, 若发生过优先级继承，释放时会恢复持有者原始优先级
 * @param[in] mutex 互斥量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误或非持有者释放
 */
mrtk_err_t mrtk_mutex_release(mrtk_mutex_t *mutex);

/**
 * @brief 互斥量属性控制
 * @details 查询或修改互斥量属性
 * @param[in]     mutex 互斥量对象指针
 * @param[in]     cmd   控制命令
 * @param[in,out] arg   命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_mutex_control(mrtk_mutex_t *mutex, mrtk_u32_t cmd, mrtk_void_t *arg);

/**
 * @brief 强制唤醒 IPC 对象等待队列中的所有任务
 * @note 内部 API，请勿在应用代码中直接调用
 * @note 调用此函数前必须持有中断锁
 * @param[in] mutex 互斥量对象指针
 * @return mrtk_bool_t 是否需要触发调度（MRTK_TRUE：需要，MRTK_FALSE：不需要）
 */
mrtk_bool_t _mrtk_mutex_force_release(mrtk_mutex_t *mutex);

#if (MRTK_DEBUG == 1)
/**
 * @brief 导出互斥量状态信息到控制台
 * @details 打印互斥量的名称、锁定状态、持有者、优先级继承等调试信息
 * @note 需要开启 MRTK_DEBUG 配置宏
 * @param[in] mutex 互斥量控制块指针
 */
mrtk_void_t mrtk_mutex_dump(mrtk_mutex_t *mutex);
#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_MUTEX_H__ */
