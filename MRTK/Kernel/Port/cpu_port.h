#include "mrtk_typedef.h"

/* Cortex-M 栈 */
#define MRTK_ARCH_STACK_ALIGN_SIZE 8 // 对齐系数
#define MRTK_ARCH_STACK_GROWS_DOWN 1 // 1=向下生长，0=向上生长
/* Cortex-M 中断控制和状态寄存器 (ICSR) 地址 */
#define MRTK_NVIC_INT_CTRL (*((volatile mrtk_u32_t *) 0xE000ED04))
/* PendSV 设置位 (Bit 28) */
#define MRTK_NVIC_PENDSVSET (1UL << 28)

struct stack_frame {
    mrtk_u32_t r4;
    mrtk_u32_t r5;
    mrtk_u32_t r6;
    mrtk_u32_t r7;
    mrtk_u32_t r8;
    mrtk_u32_t r9;
    mrtk_u32_t r10;
    mrtk_u32_t r11;

    mrtk_u32_t r0;
    mrtk_u32_t r1;
    mrtk_u32_t r2;
    mrtk_u32_t r3;
    mrtk_u32_t r12;
    mrtk_u32_t lr;
    mrtk_u32_t pc;
    mrtk_u32_t xpsr;
};

/**
 * @brief 硬件栈初始化
 * @param[in] entry 任务入口函数
 * @param[in] parameter 任务入口函数参数
 * @param[in] stack_top 已经对齐好的栈顶地址 (High Address)
 * @param[in] exit 退出函数地址 (LR)
 * @return mrtk_void_t* 伪造现场后的新栈指针 (SP)
 */
mrtk_void_t *mrtk_hw_stack_init(mrtk_void_t *entry, mrtk_void_t *parameter, mrtk_void_t *stack_top,
                                mrtk_void_t *exit);

/**
 * @brief 关闭全局中断
 * @return mrtk_base_t 关闭前的中断状态 (PRIMASK 寄存器的值)
 */
mrtk_base_t mrtk_hw_interrupt_disable(void);

/**
 * @brief 恢复全局中断
 * @param[in] level 之前保存的中断状态
 */
void mrtk_hw_interrupt_enable(mrtk_base_t level);

/**
 * @brief 在中断中触发上下文切换
 * @note  原理是挂起 PendSV 异常
 */
void mrtk_hw_context_switch_interrupt(void);