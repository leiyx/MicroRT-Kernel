/**
 * @file mrtk_config_internal.h
 * @brief 内部配置校验与默认值补充
 * @details 本文件由内核内部使用，包含配置校验和默认值补充逻辑。
 *          本文件会被 mrtk_typedef.h 包含，不要直接包含此文件。
 * @note 用户应通过 mrtk.h 间接使用此文件
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_CONFIG_INTERNAL_H__
#define __MRTK_CONFIG_INTERNAL_H__

/* 必须首先包含用户配置文件 */
#include "mrtk_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * 默认值补充 (Default Values Fallback)
 *-----------------------------------------------------------------------------*/

#ifndef MRTK_CPU_HAS_CLZ
#define MRTK_CPU_HAS_CLZ 0
#endif

#ifndef MRTK_USING_MEM_HEAP
#define MRTK_USING_MEM_HEAP 0
#endif

#ifndef MRTK_USING_MEM_POOL
#define MRTK_USING_MEM_POOL 0
#endif

#ifndef MRTK_USING_TIMER
#define MRTK_USING_TIMER 0
#endif

#ifndef MRTK_USING_TIMER_SOFT
#define MRTK_USING_TIMER_SOFT 0
#endif

#ifndef MRTK_USING_SEMAPHORE
#define MRTK_USING_SEMAPHORE 0
#endif

#ifndef MRTK_USING_MUTEX
#define MRTK_USING_MUTEX 0
#endif

#ifndef MRTK_USING_MESSAGE_QUEUE
#define MRTK_USING_MESSAGE_QUEUE 0
#endif

#ifndef MRTK_USING_MAILBOX
#define MRTK_USING_MAILBOX 0
#endif

#ifndef MRTK_USING_TIME_SLICE
#define MRTK_USING_TIME_SLICE 0
#endif

#ifndef MRTK_USING_ASSERT
#define MRTK_USING_ASSERT 0
#endif

#ifndef MRTK_DEBUG
#define MRTK_DEBUG 0
#endif

#ifndef MRTK_HEAP_SIZE
#define MRTK_HEAP_SIZE 32 * 1024
#endif

#ifndef MRTK_PRIO_HIGHER_IS_LOWER_VALUE
#define MRTK_PRIO_HIGHER_IS_LOWER_VALUE 1
#endif

/*-----------------------------------------------------------------------------
 * 严格依赖报错 (Strict Dependency Assertions)
 *-----------------------------------------------------------------------------*/
/* * 架构规范：严禁在后台静默开启宏并消耗 RAM。
 * 如果发生依赖断层，直接触发 #error 逼迫用户去 mrtk_config.h 显式确认。
 */

#if (MRTK_USING_MEM_POOL == 1) && (MRTK_USING_MEM_HEAP == 0)
#error "Configuration Error: [MRTK_USING_MEM_POOL] requires [MRTK_USING_MEM_HEAP] to be 1!"
#endif

#if (MRTK_USING_TIMER == 1) && (MRTK_USING_MEM_HEAP == 0)
#error "Configuration Error: [MRTK_USING_TIMER] requires [MRTK_USING_MEM_HEAP] to be 1!"
#endif

#if (MRTK_USING_TIMER_SOFT == 1) && (MRTK_USING_TIMER == 0)
#error "Configuration Error: [MRTK_USING_TIMER_SOFT] requires core [MRTK_USING_TIMER] to be 1!"
#endif

#if (MRTK_USING_TIMER_SOFT == 1) && (MRTK_USING_SEMAPHORE == 0)
#error "Configuration Error: Timer Daemon requires [MRTK_USING_SEMAPHORE] to be 1 for wakeup!"
#endif

/* 如果开启了 IPC 通信机制，通常会隐式依赖 Tick 或者特定的核心组件（视你的具体实现而定） */
/* 此处保留你的原始逻辑，将 IPC 与 HEAP/TIMER 绑定。如果你的 IPC 设计是静态分配的，此处可酌情修改 */
#if (MRTK_USING_SEMAPHORE == 1) || (MRTK_USING_MUTEX == 1) || (MRTK_USING_MESSAGE_QUEUE == 1) ||   \
    (MRTK_USING_MAILBOX == 1) || (MRTK_USING_EVENT == 1)
#if (MRTK_USING_MEM_HEAP == 0)
#error "Configuration Error: IPC objects require [MRTK_USING_MEM_HEAP] for creation!"
#endif
#if (MRTK_USING_TIMER == 0)
#error "Configuration Error: IPC blocking mechanisms require [MRTK_USING_TIMER] for timeouts!"
#endif
#endif

/*-----------------------------------------------------------------------------
 * 致命架构参数校验 (Critical Architecture Validation)
 *-----------------------------------------------------------------------------*/

#ifndef MRTK_ALIGN_SIZE
#error "Critical Error: [MRTK_ALIGN_SIZE] is completely missing from mrtk_config.h!"
#elif (MRTK_ALIGN_SIZE != 8) && (MRTK_ALIGN_SIZE != 4)
#error "Critical Error: [MRTK_ALIGN_SIZE] must be 4 or 8 for ARM Cortex-M architecture!"
#endif

#ifndef MRTK_MAX_PRIO_LEVEL_NUM
#error "Critical Error: [MRTK_MAX_PRIO_LEVEL_NUM] is completely missing from mrtk_config.h!"
#elif (MRTK_MAX_PRIO_LEVEL_NUM != 32)
#error "Critical Error: [MRTK_MAX_PRIO_LEVEL_NUM] must be strictly 32 for the current bitmap scheduler!"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_CONFIG_INTERNAL_H__ */
