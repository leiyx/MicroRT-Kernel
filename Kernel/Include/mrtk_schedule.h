/**
 * @file mrtk_schedule.h
 * @author leiyx
 * @brief 调度器模块接口定义
 * @details 实现基于优先级的抢占式调度器，支持 32 级优先级，O(1) 调度算法
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_SCHEDULE_H__
#define __MRTK_SCHEDULE_H__

#include "mrtk_config_internal.h"
#include "mrtk_task.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 空闲任务优先级始终最低，具体数值视优先级方向配置宏而定 */
#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == 1)
#define MRTK_IDLE_PRIORITY (MRTK_MAX_PRIO_LEVEL_NUM - 1)
#else
#define MRTK_IDLE_PRIORITY 0
#endif

/**
 * @brief 全局内核对象链表头数组
 * @details 按内核对象类型（任务、信号量、互斥量等）分类管理所有内核对象。
 *          每个类型的对象都挂载在对应的链表上，便于调试和对象追踪。
 * @note 数组索引为 mrtk_obj_type_t 枚举值
 */
extern mrtk_list_t g_obj_list[MRTK_OBJ_TYPE_BUTT];

/**
 * @brief 已终止任务的链表头
 * @details 存储所有已删除但尚未释放资源的任务，用于任务删除后的资源管理和清理。
 * @note 终止任务在调用 mrtk_task_delete() 后会被挂载到此链表
 */
extern mrtk_list_t g_defunct_task_list;

/**
 * @brief 就绪任务队列数组（按优先级索引）
 * @details 每个优先级对应一个双向链表，存储该优先级下所有就绪状态的任务。
 *          支持快速的 O(1) 任务插入和移除操作。
 * @note 索引值即为优先级，每个链表为 FIFO结构，同优先级任务按插入顺序执行
 */
extern mrtk_list_node_t g_ready_task_list[MRTK_MAX_PRIO_LEVEL_NUM];

/**
 * @brief 就绪任务优先级位图
 * @details 32 位位图，每一位对应一个优先级级别，如bit 0 对应优先级 0
 *          当某优先级有就绪任务时，对应位置 1，否则为 0。
 * @note 配合 CLZ 指令或查找表实现 O(1) 最高优先级查找
 */
extern volatile mrtk_u32_t g_ready_prio_bitmap;

/**
 * @brief 调度锁嵌套计数器
 * @details 记录调度锁的嵌套层数，用于支持临界区保护。
 *          大于 0 时表示调度器被锁定，不允许任务切换。
 * @note 支持嵌套锁定，调用 mrtk_schedule_lock() 时加 1，
 *       调用 mrtk_schedule_unlock() 时减 1，归零时才真正解锁调度器
 * @warning 调度锁锁定期间，调度请求会被延迟到解锁时执行，不会丢失
 */
extern volatile mrtk_u32_t g_schedule_lock_nest;

/**
 * @brief 延迟调度标志位
 * @details 当在中断中触发了任务调度请求时，设置此标志为 1。
 * 在离开最外层中断时检查此标志，如果为 1 则触发任务切换。
 * @note 该标志确保调度请求只在合适的时机（退出最外层中断时）执行
 */
extern volatile mrtk_u8_t g_need_schedule;

/**
 * @brief 当前运行任务的 TCB 指针（供汇编代码使用）
 * @details 在任务切换时由Port层汇编代码直接读写，指向当前任务的 TCB。
 * @note 使用 volatile 修饰，供Port层汇编上下文切换代码使用
 */
extern mrtk_tcb_t *volatile g_CurrentTCB;

/**
 * @brief 下一个要运行的任务的 TCB 指针（供汇编代码使用）
 * @details 在任务切换前由调度器设置，指向即将切换到的目标任务。
 * @note 使用 volatile 修饰，供Port层汇编上下文切换代码使用
 */
extern mrtk_tcb_t *volatile g_NextTCB;

/**
 * @brief 系统启动标志
 * @details 标识系统是否已经正式点火启动。
 *          在系统未启动前，调度器不执行上下文切换，仅设置延迟调度标志。
 *          此标志在 mrtk_system_start() 中设置为 MRTK_TRUE。
 * @note 默认为 MRTK_FALSE，系统点火后置位
 */
extern volatile mrtk_u8_t g_mrtk_started;

/**
 * @brief 初始化调度器
 * @return mrtk_err_t 错误码
 */
mrtk_err_t mrtk_schedule_init(void);

/**
 * @brief 触发一次任务调度
 */
mrtk_void_t mrtk_schedule(mrtk_void_t);

/**
 * @brief 将任务添加到对应就绪队列，O(1)操作
 * @param[in] task 目标任务
 */
mrtk_void_t _mrtk_schedule_insert_task(mrtk_task_t *task);

/**
 * @brief 将任务从对应就绪队列中移除，O(1)操作
 * @param[in] task 目标任务
 */
mrtk_void_t _mrtk_schedule_remove_task(mrtk_task_t *task);

/**
 * @brief 优先级比较，lhs优先级是否比rhs优先级更高
 * @param[in] lhs 任务
 * @param[in] rhs 任务（可以为 NULL，表示系统未启动）
 * @retval MRTK_TRUE lhs 优先级高于 rhs，或 rhs 为 NULL
 * @retval MRTK_FALSE lhs 优先级低于或等于 rhs
 * @note 当 rhs 为 NULL 时，认为 lhs 优先级更高（需要调度）
 */
static inline mrtk_bool_t mrtk_schedule_prio_ht(mrtk_task_t *lhs, mrtk_task_t *rhs)
{
    /* 处理 rhs 为 NULL 的情况（系统未启动时 g_CurrentTCB 为 NULL） */
    if (rhs == MRTK_NULL) {
        return MRTK_TRUE;
    }

#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == MRTK_TRUE)
    /* 数值越小优先级越高 (0 为最高优先级) */
    return (lhs->priority < rhs->priority) ? MRTK_TRUE : MRTK_FALSE;
#else
    /* 数值越大优先级越高（MRTK_MAX_PRIO_LEVEL_NUM - 1 为最高优先级） */
    return (lhs->priority > rhs->priority) ? MRTK_TRUE : MRTK_FALSE;
#endif
}

/**
 * @brief 优先级比较，lhs优先级是否比rhs优先级更低
 * @param[in] lhs 任务
 * @param[in] rhs 任务（可以为 NULL，表示系统未启动）
 * @retval MRTK_TRUE lhs 优先级低于 rhs
 * @retval MRTK_FALSE lhs 优先级高于或等于 rhs，或 rhs 为 NULL
 * @note 当 rhs 为 NULL 时，认为 lhs 优先级不低于 rhs（不需要调度）
 */
static inline mrtk_bool_t mrtk_schedule_prio_lt(mrtk_task_t *lhs, mrtk_task_t *rhs)
{
    /* 处理 rhs 为 NULL 的情况（系统未启动时 g_CurrentTCB 为 NULL） */
    if (rhs == MRTK_NULL) {
        return MRTK_FALSE;
    }

#if (MRTK_PRIO_HIGHER_IS_LOWER_VALUE == MRTK_TRUE)
    /* 数值越小优先级越高 (0 为最高优先级) */
    return (lhs->priority > rhs->priority) ? MRTK_TRUE : MRTK_FALSE;
#else
    /* 数值越大优先级越高（MRTK_MAX_PRIO_LEVEL_NUM - 1 为最高优先级） */
    return (lhs->priority < rhs->priority) ? MRTK_TRUE : MRTK_FALSE;
#endif
}

/**
 * @brief 锁定调度器
 * @details 锁定调度器后，任务的挂起、恢复、优先级改变等操作仍然有效，
 *          但不会触发实际的上下文切换，直到解锁调度器。
 * @note 支持嵌套调用，必须与 mrtk_schedule_unlock 配对使用。
 * @warning 禁止长时间锁定调度器，会影响系统实时性。
 */
mrtk_void_t mrtk_schedule_lock(mrtk_void_t);

/**
 * @brief 解锁调度器
 * @details 如果在锁定期间有调度请求，则只有在最外层解锁时才触发调度。
 * @note 支持嵌套调用，必须与 mrtk_schedule_lock 配对使用。
 */
mrtk_void_t mrtk_schedule_unlock(mrtk_void_t);

/**
 * @brief 修改任务优先级
 * @details 将任务从当前优先级队列中移除，修改优先级后插入到新优先级队列。
 *          非线程安全，不会触发调度。
 * @param[in] task 目标任务
 * @param[in] prio 新的优先级 (0 - MRTK_MAX_PRIO_LEVEL_NUM-1，数值越小优先级越高)
 */
mrtk_void_t _mrtk_schedule_prio_change(mrtk_task_t *task, mrtk_u32_t prio);

/**
 * @brief 获取当前就绪表中优先级最高的任务
 * @details 实现了 O(1) 时间复杂度的查找算法
 *          实现方式由 mrtk_config.h 中的配置决定：
 *          - MRTK_CPU_HAS_CLZ = 1：使用硬件 CLZ/CTZ 指令（如Cortex-M3/M4/M7）
 *          - MRTK_CPU_HAS_CLZ = 0：使用 256 字节查找表（如Cortex-M0/M0+）
 *          - MRTK_PRIO_HIGHER_IS_LOWER_VALUE = 1：数值越小优先级越高（查找最低置位位）
 *          - MRTK_PRIO_HIGHER_IS_LOWER_VALUE = 0：数值越大优先级越高（查找最高置位位）
 * @return mrtk_u8_t 最高优先级
 */
mrtk_u8_t _mrtk_schedule_get_highest_prio(mrtk_void_t);

/**
 * @brief 系统滴答递增函数
 * @details 由 SysTick 中断周期性调用，负责：
 *          - 递增全局 Tick 计数器
 *          - 扫描硬定时器链表，触发到期定时器
 *          - 处理同优先级任务的轮转调度（时间片用尽时切换到下一个任务）
 * @note 此函数在中断上下文中调用，需要关中断保护临界区
 * @warning 调用此函数前必须确保已在 SysTick_Handler 中调用 mrtk_interrupt_enter()
 */
mrtk_void_t mrtk_tick_increase(mrtk_void_t);

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_SCHEDULE_H__ */