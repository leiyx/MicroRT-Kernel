#ifndef MRTK_IRQ_H
#define MRTK_IRQ_H

#include "cpu_port.h"

#define mrtk_enter_critical()   mrtk_hw_interrupt_disable()   /**< 进入临界区 */
#define mrtk_exit_critical(lvl) mrtk_hw_interrupt_enable(lvl) /**< 离开临界区 */

extern volatile mrtk_u8_t g_interrupt_nest;
/* 因为中断优先级高于一切任务，即使中断中触发了任务调度，如果此时g_interrupt_nest > 0
 * ,则只能先设置标识位，等到 离开最外层中断 时（g_interrupt_nest == 0）再处理
 */
extern volatile mrtk_u8_t g_need_schedule;

/**
 * @brief 进入中断头部处理 (在 ISR 头部调用)
 */
void mrtk_interrupt_enter(void);

/**
 * @brief 中断尾部处理 (在 ISR 尾部调用)
 * @note 用户编写ISR时，需遵守如下规范：mrtk_interrupt_enter(); xxxx; mrtk_interrupt_leave();
 */
void mrtk_interrupt_leave(void);

#endif // MRTK_IRQ_H