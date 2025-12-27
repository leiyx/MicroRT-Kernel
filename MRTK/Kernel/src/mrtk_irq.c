#include "mrtk_irq.h"

volatile mrtk_u8_t g_interrupt_nest = 0;
volatile mrtk_u8_t g_need_schedule  = 0;

void mrtk_interrupt_enter(void)
{
    mrtk_base_t primask = mrtk_enter_critical();
    ++g_interrupt_nest;
    mrtk_exit_critical(primask);
}

void mrtk_interrupt_leave(void)
{
    mrtk_base_t primask = mrtk_enter_critical();
    --g_interrupt_nest;
    if (g_interrupt_nest == 0 && g_need_schedule == 1) {
        // 触发调度(触发PendSV异常)，并复位g_need_schedule
        mrtk_hw_context_switch_interrupt();
        g_need_schedule = 0;
    }
    mrtk_exit_critical(primask);
}