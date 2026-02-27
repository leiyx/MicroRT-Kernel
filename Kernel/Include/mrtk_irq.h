/**
 * @file mrtk_irq.h
 * @author leiyx
 * @brief 中断管理模块接口定义
 * @details 提供中断嵌套管理和中断上下文检测功能
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_IRQ_H__
#define __MRTK_IRQ_H__

#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "cpu_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 全局变量声明
 * ============================================================================== */

/**
 * @brief 中断嵌套计数器
 * @details 用于跟踪当前中断嵌套层数，每次进入中断递增，退出中断递减
 * @note 该变量用于确保在离开最外层中断时才进行任务调度
 * @note 中断优先级高于一切任务，即使中断中触发了任务调度请求，
 *       如果此时 g_interrupt_nest > 0，则只能先设置标志位，
 *       等到离开最外层中断时（g_interrupt_nest == 0）再处理
 */
extern volatile mrtk_u8_t g_interrupt_nest;

/* ==============================================================================
 * 中断管理 API
 * ============================================================================== */

/**
 * @brief 中断头部处理
 * @details 必须在中断服务程序（ISR）的头部调用，用于递增中断嵌套计数器
 * @note 用户编写 ISR 时，需遵守规范：
 *       - ISR 开头调用 mrtk_interrupt_enter()
 *       - ISR 结尾调用 mrtk_interrupt_leave()
 */
mrtk_void_t mrtk_interrupt_enter(mrtk_void_t);

/**
 * @brief 中断尾部处理
 * @details 必须在中断服务程序（ISR）的尾部调用，用于递减中断嵌套计数器
 *          如果在离开最外层中断时检测到调度请求，将触发任务切换
 * @note 用户编写 ISR 时，需遵守规范：
 *       - ISR 开头调用 mrtk_interrupt_enter()
 *       - ISR 结尾调用 mrtk_interrupt_leave()
 */
mrtk_void_t mrtk_interrupt_leave(mrtk_void_t);

/**
 * @brief 中断管理初始化
 */
mrtk_void_t mrtk_irq_init(mrtk_void_t);

/**
 * @brief 获取当前中断嵌套层数
 * @return mrtk_u8_t 当前中断嵌套计数，0 表示未进入中断
 */
static inline mrtk_u8_t mrtk_irq_get_nest(mrtk_void_t)
{
    return g_interrupt_nest;
}

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_IRQ_H__ */