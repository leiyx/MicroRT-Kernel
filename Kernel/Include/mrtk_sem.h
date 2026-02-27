/**
 * @file mrtk_sem.h
 * @author leiyx
 * @brief 信号量管理模块接口定义
 * @details 提供计数信号量功能，用于任务间同步和资源互斥访问
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_SEM_H__
#define __MRTK_SEM_H__

#include "mrtk_config_internal.h"
#include "mrtk_errno.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 信号量最大计数值，防止 release 溢出 */
#define MRTK_SEM_MAX_VALUE 0xFFFF

/* ==============================================================================
 * 信号量控制命令定义
 * ============================================================================== */

/**
 * @brief 信号量控制命令枚举
 * @details 用于 mrtk_sem_control() 的 cmd 参数
 */
typedef enum {
    MRTK_SEM_CMD_GET_VALUE = 0x00, /**< 获取当前信号量计数值 (arg = mrtk_u16_t *) */
} mrtk_sem_cmd_t;

/* ==============================================================================
 * 信号量对象定义
 * ============================================================================== */

/**
 * @brief 信号量结构体
 * @details 继承自 IPC 对象基类，实现资源计数和阻塞等待机制
 */
typedef struct mrtk_sem_def {
    mrtk_ipc_obj_t ipc_obj;  /**< IPC 对象基类 */
    mrtk_u16_t     value;    /**< 信号量资源计数值 */
    mrtk_u16_t     reserved; /**< 保留字段（用于内存对齐） */
} mrtk_sem_t;

/* ==============================================================================
 * 信号量管理 API
 * ============================================================================== */

/**
 * @brief 信号量静态初始化
 * @details 初始化信号量对象，设置初始资源计数和唤醒策略
 * @param[in] sem   信号量对象指针
 * @param[in] name  信号量名称
 * @param[in] value 初始资源计数值
 * @param[in] flag  等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_init(mrtk_sem_t *sem, const mrtk_char_t *name, mrtk_u16_t value,
                         mrtk_u8_t flag);

/**
 * @brief 信号量静态脱离
 * @details 从系统对象管理中移除信号量，唤醒所有等待任务
 * @param[in] sem 信号量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_detach(mrtk_sem_t *sem);

/**
 * @brief 信号量动态创建
 * @details 从内存堆中分配信号量对象并初始化
 * @param[in] name  信号量名称
 * @param[in] value 初始资源计数值
 * @param[in] flag  等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @return mrtk_sem_t* 成功返回信号量指针，失败返回 MRTK_NULL
 */
mrtk_sem_t *mrtk_sem_create(const mrtk_char_t *name, mrtk_u16_t value, mrtk_u8_t flag);

/**
 * @brief 信号量动态删除
 * @details 释放信号量对象占用的内存，唤醒所有等待任务
 * @param[in] sem 信号量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_delete(mrtk_sem_t *sem);

/**
 * @brief 获取信号量（带超时等待）
 * @details 尝试获取信号量资源，若资源不足则阻塞等待
 * @param[in] sem     信号量对象指针
 * @param[in] timeout 等待超时时间（单位为 Tick）
 *                    0 表示立即返回，MRTK_WAIT_FOREVER 表示永久等待
 * @retval MRTK_EOK    成功获取
 * @retval MRTK_EEMPTY 资源不足且超时
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_take(mrtk_sem_t *sem, mrtk_s32_t timeout);

/**
 * @brief 尝试获取信号量（立即返回）
 * @details 非阻塞方式尝试获取信号量资源
 * @param[in] sem 信号量对象指针
 * @retval MRTK_EOK    成功获取
 * @retval MRTK_EEMPTY 资源不足
 * @retval MRTK_EINVAL 参数错误
 */
#define mrtk_sem_trytake(sem) mrtk_sem_take(sem, 0)

/**
 * @brief 释放/发布信号量
 * @details 信号量资源计数加 1，如果有等待任务则唤醒其中一个
 * @param[in] sem 信号量对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_release(mrtk_sem_t *sem);

/**
 * @brief 信号量属性控制
 * @details 查询或修改信号量属性
 * @param[in]     sem 信号量对象指针
 * @param[in]     cmd 控制命令
 * @param[in,out] arg 命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_sem_control(mrtk_sem_t *sem, mrtk_u32_t cmd, mrtk_void_t *arg);

#if (MRTK_DEBUG == 1)
/**
 * @brief 导出信号量状态信息到控制台
 * @details 打印信号量的名称、计数值、等待队列等调试信息
 * @note 需要开启 MRTK_DEBUG 配置宏
 * @param[in] sem 信号量控制块指针
 */
mrtk_void_t mrtk_sem_dump(mrtk_sem_t *sem);
#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_SEM_H__ */