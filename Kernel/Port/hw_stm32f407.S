.syntax unified
.cpu cortex-m4
.thumb

.section .text
.extern g_CurrentTCB
.extern g_NextTCB

@ -----------------------------------------------------------------------
@ PendSV_Handler: 统一处理启动与切换
@ TCB stack_ptr 偏移量计算 (0x1C = 28 字节): 临时方案TODO
@   mrtk_obj_t obj:  0-27  (28字节: name[16]=16, type=1, flag=1, obj_node=8)
@   stack_ptr:        28    ← 目标成员
@ -----------------------------------------------------------------------
@ -----------------------------------------------------------------------
.global PendSV_Handler
.type PendSV_Handler, %function
PendSV_Handler:
    CPSID   I                       @ 关中断
    MRS     R0, PSP                 @ 获取任务栈指针
    CBZ     R0, skip_save           @ 若 PSP 为 0 则说明是首次启动，跳过保存

    STMDB   R0!, {R4-R11}           @ 保存 R4-R11
    LDR     R1, =g_CurrentTCB
    LDR     R1, [R1]
    STR     R0, [R1, #28]    @ 更新 TCB 栈指针

skip_save:
    LDR     R1, =g_CurrentTCB
    LDR     R2, =g_NextTCB
    LDR     R2, [R2]
    STR     R2, [R1]                @ g_CurrentTCB = g_NextTCB

    LDR     R0, [R2, #28]    @ 加载新任务栈指针
    LDMIA   R0!, {R4-R11}           @ 弹出 R4-R11
    MSR     PSP, R0                 @ 更新 PSP

    MOV     LR, #0xFFFFFFFD         @ 异常返回使用 PSP
    CPSIE   I
    BX      LR

@ -----------------------------------------------------------------------
@ mrtk_start: 系统“打火机”
@ -----------------------------------------------------------------------
.global mrtk_start
.type mrtk_start, %function
mrtk_start:
    MOV     R0, #0
    MSR     PSP, R0                 @ 预设 PSP 为 0 触发 skip_save
    LDR     R0, =0xE000ED04
    LDR     R1, =(1 << 28)
    STR     R1, [R0]                @ 触发 PendSV
    CPSIE   I
wait_loop:
    B       wait_loop