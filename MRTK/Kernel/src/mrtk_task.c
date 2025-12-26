#include "mrtk_task.h"
#include "mrtk_typedef.h"
mrtk_tcb_t *volatile g_CurrentTCB = 0;
mrtk_tcb_t *volatile g_NextTCB    = 0;

// 外部引用任务控制块
extern mrtk_tcb_t TaskA_TCB, TaskB_TCB, Idle_TCB;

mrtk_void_t mrtk_task_create(mrtk_tcb_t *tcb, mrtk_void_t (*taskPtr)(mrtk_void_t),
                             mrtk_u32_t *stackLimit)
{
    mrtk_u32_t *stk = (mrtk_u32_t *) ((mrtk_u32_t) stackLimit & ~0x07); // 8字节对齐

    *(--stk) = 0x01000000;           // xPSR: Thumb 模式
    *(--stk) = (mrtk_u32_t) taskPtr; // PC: 任务入口
    *(--stk) = 0xFFFFFFFD;           // LR: 异常返回模式

    for (int i = 0; i < 5; i++) {
        *(--stk) = 0; // R12, R3, R2, R1, R0
    }
    for (int i = 0; i < 8; i++) {
        *(--stk) = 0; // R11 - R4
    }

    tcb->stackPtr   = stk;
    tcb->delayTicks = 0;
}

// 抢占式调度逻辑：B > A > Idle
mrtk_void_t mrtk_schedule(mrtk_void_t)
{
    mrtk_tcb_t *best_task = &Idle_TCB; // 默认指向空闲任务

    if (TaskB_TCB.delayTicks == 0) {
        best_task = &TaskB_TCB;
    } else if (TaskA_TCB.delayTicks == 0) {
        best_task = &TaskA_TCB;
    }

    if (g_CurrentTCB != best_task) {
        g_NextTCB                             = best_task;
        *((mrtk_u32_t volatile *) 0xE000ED04) = (1 << 28); // 触发 PendSV
    }
}

mrtk_void_t mrtk_delay(mrtk_u32_t ticks)
{
    g_CurrentTCB->delayTicks = ticks;
    mrtk_schedule(); // 主动放弃 CPU
}

mrtk_void_t mrtk_tick_handler(mrtk_void_t)
{
    // 递减所有任务的延时计数器
    if (TaskA_TCB.delayTicks > 0) {
        TaskA_TCB.delayTicks--;
    }
    if (TaskB_TCB.delayTicks > 0) {
        TaskB_TCB.delayTicks--;
    }
    mrtk_schedule(); // 每 1ms 检查一次抢占
}