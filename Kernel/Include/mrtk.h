/**
 * @file mrtk.h
 * @author Leiyx
 * @brief MRTK 主入口头文件 (对标 FreeRTOS.h)
 * @details
 * 本文件是 MRTK 的唯一入口头文件，用户只需包含此文件即可使用所有 MRTK 功能。
 *
 * 本文件会自动：
 * 1. 包含用户配置文件 (mrtk_config.h)
 * 2. 补充默认值并校验依赖关系
 * 3. 包含所有必要的内核头文件
 *
 * 用户使用方式：
 * @code
 *   #include "mrtk.h"  // 包含所有 MRTK 功能
 * @endcode
 *
 * @date 2026-02-28
 * @version 0.4
 */

#ifndef __MRTK_H__
#define __MRTK_H__

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * 配置层 (Configuration Layer)
 *-----------------------------------------------------------------------------*/
#include "mrtk_config_internal.h"

/*-----------------------------------------------------------------------------
 * 基础类型层 (Base Types Layer)
 *-----------------------------------------------------------------------------*/
#include "mrtk_errno.h"
#include "mrtk_typedef.h"
#include "mrtk_utils.h"

/*-----------------------------------------------------------------------------
 * 核心数据结构层 (Core Data Structures Layer)
 *-----------------------------------------------------------------------------*/
#include "mrtk_list.h"
#include "mrtk_obj.h"

/*-----------------------------------------------------------------------------
 * 核心模块层 (Core Modules Layer)
 *-----------------------------------------------------------------------------*/
#include "mrtk_ipc_obj.h"
#include "mrtk_irq.h"
#include "mrtk_schedule.h"
#include "mrtk_task.h"

/*-----------------------------------------------------------------------------
 * 功能模块层 (Feature Modules Layer)
 * 根据用户配置条件包含各个功能模块
 *-----------------------------------------------------------------------------*/

#if (MRTK_USING_TIMER == 1)
#include "mrtk_timer.h"
#endif

#if (MRTK_USING_SEMAPHORE == 1)
#include "mrtk_sem.h"
#endif

#if (MRTK_USING_MUTEX == 1)
#include "mrtk_mutex.h"
#endif

#if (MRTK_USING_MESSAGE_QUEUE == 1)
#include "mrtk_msg_queue.h"
#endif

#if (MRTK_USING_MAILBOX == 1)
#include "mrtk_mail_box.h"
#endif

#if (MRTK_USING_EVENT == 1)
#include "mrtk_event.h"
#endif

#if (MRTK_USING_MEM_HEAP == 1)
#include "mrtk_mem_heap.h"
#endif

#if (MRTK_USING_MEM_POOL == 1)
#include "mrtk_mem_pool.h"
#endif

#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

/**
 * @brief 系统统一初始化接口
 * @details 应用程序入口调用此函数完成内核初始化
 * @retval MRTK_EOK   初始化成功
 * @retval MRTK_ERROR 初始化失败
 */
mrtk_err_t mrtk_system_init(mrtk_void_t);

/**
 * @brief 系统启动接口
 * @details 由 main 函数在所有初始化完成后调用，触发第一次上下文切换，永不返回
 */
mrtk_void_t mrtk_system_start(mrtk_void_t);

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_H__ */
