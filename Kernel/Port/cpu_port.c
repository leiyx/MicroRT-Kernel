#include "cpu_port.h"
#include "mrtk_typedef.h"
mrtk_void_t *mrtk_hw_stack_init(mrtk_void_t *entry, mrtk_void_t *parameter, mrtk_void_t *stack_top,
                                mrtk_void_t *exit)
{
    struct stack_frame *stack_frame;
    mrtk_u8_t          *stk;

    /* 直接使用传入的 stack_top */
    stk = (mrtk_u8_t *) stack_top;

    /* 腾出空间存放上下文 (16个寄存器 * 4字节 = 64字节) */
    stk -= sizeof(struct stack_frame);
    stack_frame = (struct stack_frame *) stk;

    /* --- 1. 自动压栈部分 (Exception Frame) --- */
    /* xPSR: Bit24 必须为 1 (Thumb 状态)，否则 HardFault */
    stack_frame->xpsr = 0x01000000L;

    /* PC: 任务入口函数地址 (切记最后一位可能是1，表示Thumb) */
    stack_frame->pc = (mrtk_u32_t) entry;

    /* LR: 任务退出函数 (如果任务函数 return 了，会跳到这里) */
    stack_frame->lr = (mrtk_u32_t) exit;

    /* R12: 没什么用，初始化为 0 */
    stack_frame->r12 = 0;

    /* R0: 参数 Parameter (根据 AAPCS 标准，第一个参数在 R0) */
    stack_frame->r0 = (mrtk_u32_t) parameter;

    /* R1-R3: 初始化为 0 */
    stack_frame->r1 = 0;
    stack_frame->r2 = 0;
    stack_frame->r3 = 0;

    /* --- 2. 手动压栈部分 (Software Saved) --- */
    /* R4-R11: 填入 0xDEADBEEF 方便调试 */
    stack_frame->r4  = 0xDEADBEEF;
    stack_frame->r5  = 0xDEADBEEF;
    stack_frame->r6  = 0xDEADBEEF;
    stack_frame->r7  = 0xDEADBEEF;
    stack_frame->r8  = 0xDEADBEEF;
    stack_frame->r9  = 0xDEADBEEF;
    stack_frame->r10 = 0xDEADBEEF;
    stack_frame->r11 = 0xDEADBEEF;

    /* 返回新的栈顶指针，将来赋值给 TCB->stack_ptr */
    return (mrtk_void_t *) stk;
}

mrtk_ubase_t mrtk_hw_interrupt_disable(mrtk_void_t)
{
    mrtk_ubase_t level;

    /* 读取当前 PRIMASK 值到 level 变量 (保存当前状态) */
    __asm volatile("mrs %0, primask" : "=r"(level) : : "memory");

    /* 关闭全局中断 (Set PRIMASK = 1) */
    __asm volatile("cpsid i" : : : "memory");

    /* 返回之前的状态 */
    return level;
}

mrtk_void_t mrtk_hw_interrupt_enable(mrtk_ubase_t level)
{
    /* 恢复 PRIMASK 的值 */
    __asm volatile("msr primask, %0" : : "r"(level) : : "memory");
}

mrtk_void_t mrtk_hw_context_switch_interrupt(mrtk_void_t)
{
    MRTK_NVIC_INT_CTRL = MRTK_NVIC_PENDSVSET;
}

/**
 * @brief 硬件层字符输出接口实现（默认实现）
 * @param str 要输出的字符串（以 null 结尾）
 * @note 默认实现：使用标准库 putchar（适用于测试环境）
 * @note 实际硬件移植时，应重写此函数以输出到 UART 等设备
 * @code
 *       // STM32 UART 示例：
 *       mrtk_void_t mrtk_hw_output_string(const char *str) {
 *           while (*str) {
 *               while(!(USART1->SR & USART_SR_TXE));  // 等待发送缓冲区空
 *               USART1->DR = *str++;                  // 发送字符
 *           }
 *       }
 * @endcode
 */
mrtk_void_t mrtk_hw_output_string(const mrtk_char_t *str)
{
    if (str == MRTK_NULL) {
        return;
    }

    /* 默认实现：使用标准库 putchar（测试环境） */
    while (*str != '\0') {
        putchar((int) *str);
        str++;
    }
}
