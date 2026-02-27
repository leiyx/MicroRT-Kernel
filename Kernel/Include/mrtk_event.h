/**
 * @file mrtk_event.h
 * @author leiyx
 * @brief 事件标志组管理模块接口定义
 * @details 提供事件标志同步机制，支持多对多任务同步、逻辑与/逻辑或等待、自动清除标志
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_EVENT_H__
#define __MRTK_EVENT_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "mrtk_errno.h"
#include "mrtk_ipc_obj.h"
#include "mrtk_list.h"
#include "mrtk_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 事件标志组常量定义
 * ============================================================================== */

/**
 * @brief 事件等待选项常量
 * @details 用于 mrtk_event_recv() 的 option 参数，可通过位或组合使用
 */
/** 逻辑与：等待所有指定事件都置位 */
#define MRTK_EVENT_FLAG_AND 0x01
/** 逻辑或：等待任意指定事件置位 */
#define MRTK_EVENT_FLAG_OR 0x02
/** 自动清除：唤醒后自动清除已触发的事件标志 */
#define MRTK_EVENT_FLAG_CLEAR 0x04

/**
 * @brief 事件控制命令枚举
 * @details 用于 mrtk_event_control() 的 cmd 参数
 */
typedef enum {
    MRTK_EVENT_CMD_CLEAR = 0x01, /**< 清空所有事件标志 (arg: MRTK_NULL) */
} mrtk_event_cmd_t;

/* ==============================================================================
 * 事件标志组对象定义
 * ============================================================================== */

/**
 * @brief 事件标志组结构体
 * @details 继承自 IPC 对象基类，实现事件标志同步机制
 * @note 支持 32 位事件标志，每位代表一个独立的事件
 */
typedef struct mrtk_event_def {
    mrtk_ipc_obj_t ipc_obj; /**< IPC 对象基类 */
    mrtk_u32_t     set;     /**< 当前已触发的事件集合（32 位标志位） */
} mrtk_event_t;

/* ==============================================================================
 * 事件标志组管理 API
 * ============================================================================== */

/**
 * @brief 事件标志组静态初始化
 * @details 初始化事件标志组对象，设置唤醒策略
 * @param[out] event  事件标志组对象指针
 * @param[in]  name   事件标志组名称
 * @param[in]  flag   等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_init(mrtk_event_t *event, const mrtk_char_t *name, mrtk_u8_t flag);

/**
 * @brief 事件标志组静态脱离
 * @details 从系统对象管理中移除事件标志组，唤醒所有等待任务
 * @param[in] event 事件标志组对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_detach(mrtk_event_t *event);

/**
 * @brief 事件标志组动态创建
 * @details 从内存堆中分配事件标志组对象并初始化
 * @param[in] name 事件标志组名称
 * @param[in] flag 等待队列策略（MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO 或 PRIO）
 * @return mrtk_event_t* 成功返回事件标志组指针，失败返回 MRTK_NULL
 */
mrtk_event_t *mrtk_event_create(const mrtk_char_t *name, mrtk_u8_t flag);

/**
 * @brief 事件标志组动态删除
 * @details 释放事件标志组对象占用的内存，唤醒所有等待任务
 * @param[in] event 事件标志组对象指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_delete(mrtk_event_t *event);

/**
 * @brief 发送事件标志
 * @details 设置指定的事件标志位，唤醒等待的任务（可能多个）
 * @note 中断安全，可在中断服务程序中调用
 * @param[in] event 事件标志组对象指针
 * @param[in] set   要设置的事件位掩码
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_send(mrtk_event_t *event, mrtk_u32_t set);

/**
 * @brief 接收事件标志
 * @details 等待指定的事件标志位，支持逻辑与/逻辑或、自动清除
 * @note 阻塞 API，禁止在中断中调用
 * @param[in]  event       事件标志组对象指针
 * @param[in]  set_to_wait 等待的事件位掩码
 * @param[in]  option      等待选项（MRTK_EVENT_FLAG_AND/OR/CLEAR 组合）
 * @param[in]  timeout     超时时间（单位：Tick）
 *                         0 表示立即返回，MRTK_IPC_WAIT_FOREVER 表示永久等待
 * @param[out] recved      用于回传实际触发的事件标志
 * @retval MRTK_EOK    成功接收
 * @retval MRTK_ETIMEOUT 超时未满足条件
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_recv(mrtk_event_t *event, mrtk_u32_t set_to_wait, mrtk_u8_t option,
                           mrtk_tick_t timeout, mrtk_u32_t *recved);

/**
 * @brief 事件标志组属性控制
 * @details 查询或修改事件标志组属性
 * @param[in]     event 事件标志组对象指针
 * @param[in]     cmd   控制命令（见 mrtk_event_cmd_t 枚举）
 * @param[in,out] arg   命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_event_control(mrtk_event_t *event, mrtk_u32_t cmd, mrtk_void_t *arg);

/* ==============================================================================
 * 调试与导出 API (Debug & Dump API)
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出事件标志组状态信息到控制台
 * @details 打印事件标志组的名称、地址、当前事件集合、等待任务数量等调试信息
 * @param[in] event 事件标志组对象指针
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_event_dump(mrtk_event_t *event);

#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_EVENT_H__ */
