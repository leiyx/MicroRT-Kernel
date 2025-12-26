/**
 * @file mrtk_task.h
 * @author leiyx
 * @brief 任务管理模块
 * @version 0.1
 * @copyright Copyright (c) 2025
 */

#ifndef MRTK_TASK_H
#define MRTK_TASK_H

#include "mrtk_typedef.h"

// 任务控制块
typedef struct mrtk_tcb_def {
    mrtk_u32_t *stackPtr;   // 栈指针，必须位于首位
    mrtk_u32_t  delayTicks; // 任务阻塞延时计数器
} mrtk_tcb_t;

// 全局 TCB 指针，供汇编使用
extern mrtk_tcb_t *volatile g_CurrentTCB;
extern mrtk_tcb_t *volatile g_NextTCB;

// 内核 API
mrtk_void_t mrtk_task_create(mrtk_tcb_t *tcb, mrtk_void_t (*taskPtr)(mrtk_void_t),
                             mrtk_u32_t *stackLimit);
mrtk_void_t mrtk_start(mrtk_void_t);
mrtk_void_t mrtk_schedule(mrtk_void_t);
mrtk_void_t mrtk_delay(mrtk_u32_t ticks);
mrtk_void_t mrtk_tick_handler(mrtk_void_t);

#endif // MRTK_TASK_H