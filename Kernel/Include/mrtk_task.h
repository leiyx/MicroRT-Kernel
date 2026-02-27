/**
 * @file mrtk_task.h
 * @author leiyx
 * @brief 任务管理模块接口定义
 * @details 定义任务控制块（TCB）、任务状态枚举及任务管理 API
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_TASK_H__
#define __MRTK_TASK_H__

#include "mrtk_config_internal.h"
#include "mrtk_list.h"
#include "mrtk_obj.h"
#include "mrtk_typedef.h"

#if (MRTK_USING_TIMER == 1)
#include "mrtk_timer.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 任务控制命令定义 (Task Control Commands)
 * ============================================================================== */

/**
 * @brief 任务控制命令枚举
 * @details 用于 mrtk_task_control() 的 cmd 参数
 */
typedef enum {
    MRTK_TASK_CMD_SET_PRIORITY, /**< 设置任务优先级 (arg = mrtk_u8_t *) */
    MRTK_TASK_CMD_GET_PRIORITY, /**< 获取任务优先级 (arg = mrtk_u8_t *) */
    MRTK_TASK_CMD_GET_TIMER,    /**< 获取内置定时器 (arg = mrtk_timer_t *) */
    MRTK_TASK_CMD_SET_CLEANUP,  /**< 注册清理回调函数 (arg = mrtk_cleanup_handler_t) */
} mrtk_task_cmd_t;

/* ==============================================================================
 * 任务状态枚举
 * ============================================================================== */

/**
 * @brief 任务状态枚举
 * @details 任务在其生命周期内会处于不同的状态
 */
typedef enum {
    MRTK_TASK_STAT_INIT    = 0x00, /**< 初始化态：刚创建，栈已初始化，但未启动 */
    MRTK_TASK_STAT_READY   = 0x01, /**< 就绪态：在就绪队列中，等待调度 */
    MRTK_TASK_STAT_RUNNING = 0x02, /**< 运行态：正在 CPU 上执行 */
    MRTK_TASK_STAT_SUSPEND = 0x03, /**< 挂起态：阻塞/挂起/延时中 */
    MRTK_TASK_STAT_CLOSE   = 0x04, /**< 关闭态：僵尸态，等待资源回收 */
} mrtk_task_stat_t;

/* ==============================================================================
 * 任务控制块定义
 * ============================================================================== */

/**
 * @brief 任务清理回调函数类型
 * @param[in] para 清理回调函数的参数
 */
typedef mrtk_void_t (*mrtk_cleanup_handler_t)(mrtk_void_t *para);

/**
 * @brief 任务入口函数类型
 * @param[in] para 任务入口函数的参数
 */
typedef mrtk_void_t (*mrtk_task_entry_t)(mrtk_void_t *para);

/**
 * @brief 任务控制块（Task Control Block，TCB）
 * @details 用于描述和管理任务的所有信息，是一种内核对象
 * @note 每个任务都有唯一的 TCB，包含栈信息、调度信息、状态信息等
 */
typedef struct mrtk_tcb_def {
    mrtk_obj_t obj; /**< 内核对象基类 */

    /* 调度相关 */
    mrtk_list_node_t sched_node;  /**< 任务节点（用于挂载到就绪队列或阻塞队列） */
    mrtk_u8_t        priority;    /**< 任务优先级（数值越小优先级越高） */
    mrtk_u8_t        state;       /**< 任务状态（见 mrtk_task_stat_t 枚举） */
    mrtk_tick_t      init_tick;   /**< 任务初始时间片长度 */
    mrtk_tick_t      remain_tick; /**< 任务剩余时间片长度 */

    /* 任务栈相关 */
    mrtk_u32_t       *stack_ptr;       /**< 栈顶指针（指向当前栈顶） */
    mrtk_u32_t       *stack_base;      /**< 任务栈基址（指向栈底） */
    mrtk_u32_t        stack_size;      /**< 任务栈大小（字节） */
    mrtk_task_entry_t task_entry;      /**< 任务入口函数指针 */
    mrtk_void_t      *task_entry_para; /**< 任务入口函数参数 */

    /* 定时器相关 */
#if (MRTK_USING_TIMER == 1)
    mrtk_timer_t timer; /**< 内置定时器指针（用于实现睡眠和超时等待） */
#endif

    /* IPC 相关：用于事件标志组等待 */
#if (MRTK_USING_EVENT == 1)
    mrtk_u32_t  event_set;    /**< 任务期望等待的事件位掩码 */
    mrtk_u8_t   event_option; /**< 事件等待选项（AND/OR/CLEAR） */
    mrtk_u32_t *event_recved; /**< 用于回传实际触发的事件标志 */
#endif

    /* 互斥量相关：用于优先级继承 */
#if (MRTK_USING_MUTEX == 1)
    /**
     * @brief 任务持有的互斥量链表
     * @details 用于支持多互斥量场景下的优先级继承
     * @note 当任务持有多个互斥量时，释放其中一个需要遍历此链表，
     *       找出其他持有互斥量的最高继承优先级作为恢复目标
     */
    mrtk_list_t held_mutex_list; /**< 任务当前持有的所有互斥量 */
    mrtk_u8_t   orig_prio;       /**< 任务原始优先级（用于优先级继承恢复） */
#endif

    /* 错误和清理相关 */
    mrtk_u32_t             last_error;           /**< 最近一次错误码 */
    mrtk_cleanup_handler_t cleanup_handler;      /**< 用户级清理回调函数 */
    mrtk_void_t           *cleanup_handler_para; /**< 清理回调函数参数 */
} mrtk_tcb_t, mrtk_task_t;

/* ==============================================================================
 * 全局变量声明（供汇编代码使用）
 * ============================================================================== */

/**
 * @brief 当前运行任务的 TCB 指针（供汇编代码使用）
 * @note 该指针始终指向当前 CPU 正在执行的任务
 */
extern mrtk_tcb_t *volatile g_CurrentTCB;

/**
 * @brief 下一个要运行的任务的 TCB 指针（供汇编代码使用）
 * @note 在调度器中设置此指针，PendSV 异常处理程序会切换到该任务
 */
extern mrtk_tcb_t *volatile g_NextTCB;

/* ==============================================================================
 * 生命周期管理 API (Lifecycle Management)
 * ============================================================================== */

/**
 * @brief 静态任务初始化
 * @details 初始化用户提供的 TCB 和栈内存，并将其挂载到内核对象链表
 * @note 不分配内存，由用户提供 TCB 和栈内存
 * @param[in]  name            任务名称
 * @param[in]  task            任务控制块指针（用户提供的内存）
 * @param[in]  entry           任务入口函数
 * @param[in]  para            任务入口函数参数
 * @param[in]  stack_base      任务栈基址
 * @param[in]  stack_size      任务栈大小（字节）
 * @param[in]  priority        任务优先级（0-31，0 最高）
 * @param[in]  tick            任务时间片长度（0 表示使用默认值）
 * @retval MRTK_EOK    初始化成功
 * @retval MRTK_EINVAL 参数错误（task、entry、stack_base 为 MRTK_NULL 或 priority 超出范围）
 */
mrtk_err_t mrtk_task_init(const mrtk_char_t *name, mrtk_task_t *task, mrtk_task_entry_t entry,
                          mrtk_void_t *para, mrtk_u32_t *stack_base, mrtk_u32_t stack_size,
                          mrtk_u8_t priority, mrtk_tick_t tick);

/**
 * @brief 分离静态任务
 * @details 将任务从调度器移除并脱离内核对象链表（但不释放内存）
 * @note 用于静态创建的任务，由用户负责释放 TCB 和栈内存
 * @param[in]  task            任务控制块指针
 * @retval MRTK_EOK    分离成功
 * @retval MRTK_EINVAL 参数错误（task 为 NULL）
 * @retval MRTK_ERROR 分离失败（任务正在运行）
 */
mrtk_err_t mrtk_task_detach(mrtk_task_t *task);

/**
 * @brief 动态创建任务
 * @details 内部分配 TCB 和栈内存，然后调用 mrtk_task_init
 * @note 需要开启 MRTK_USING_MEM_HEAP 配置宏
 * @param[in]  name            任务名称
 * @param[in]  entry           任务入口函数
 * @param[in]  para            任务入口函数参数
 * @param[in]  stack_size      任务栈大小（字节）
 * @param[in]  priority        任务优先级（0-31，0 最高）
 * @param[in]  tick            任务时间片长度（0 表示使用默认值）
 * @return mrtk_task_t* 任务控制块指针，失败返回 MRTK_NULL
 */
mrtk_task_t *mrtk_task_create(const mrtk_char_t *name, mrtk_task_entry_t entry, mrtk_void_t *para,
                              mrtk_u32_t stack_size, mrtk_u8_t priority, mrtk_tick_t tick);

/**
 * @brief 删除动态创建的任务
 * @details 内部调用 mrtk_task_detach，然后释放 TCB 和栈内存
 * @note 需要开启 MRTK_USING_MEM_HEAP 配置宏
 * @param[in]  task            任务控制块指针
 * @retval MRTK_EOK    删除成功
 * @retval MRTK_EINVAL 参数错误（task 为 NULL）
 * @retval MRTK_ERROR 删除失败（任务正在运行或非动态创建）
 */
mrtk_err_t mrtk_task_delete(mrtk_task_t *task);

/* ==============================================================================
 * 核心调度与状态 API (Core Scheduling & State Management)
 * ============================================================================== */

/**
 * @brief 启动任务
 * @details 将任务从 INIT 态加入就绪队列，状态变为 READY
 * @param[in]  task            任务控制块指针
 * @retval MRTK_EOK    启动成功
 * @retval MRTK_EINVAL 参数错误（task 为 NULL）
 * @retval MRTK_ERROR 启动失败（任务状态错误）
 */
mrtk_err_t mrtk_task_start(mrtk_task_t *task);

/**
 * @brief 挂起任务
 * @details 将任务从就绪队列移除，状态改为 SUSPEND
 * @note 如果挂起当前任务，会触发调度
 * @param[in]  task            目标任务指针（NULL 表示挂起当前任务）
 * @retval MRTK_EOK    挂起成功
 * @retval MRTK_EINVAL 参数错误
 * @retval MRTK_ERROR 挂起失败（任务状态错误）
 */
mrtk_err_t mrtk_task_suspend(mrtk_task_t *task);

/**
 * @brief 恢复任务
 * @details 将任务加入就绪队列，状态改为 READY
 * @param[in]  task            目标任务指针
 * @retval MRTK_EOK    恢复成功
 * @retval MRTK_EINVAL 参数错误（task 为 NULL）
 * @retval MRTK_ERROR 恢复失败（任务状态错误）
 */
mrtk_err_t mrtk_task_resume(mrtk_task_t *task);

/**
 * @brief 主动让出 CPU
 * @details 将当前任务移到同优先级队列的末尾，触发调度
 * @note 如果同优先级队列中没有其他任务，则直接返回
 * @retval MRTK_EOK    让出成功
 * @retval MRTK_ERROR 让出失败（在中断中调用）
 */
mrtk_err_t mrtk_task_yield(mrtk_void_t);

/**
 * @brief 任务相对延时
 * @details 当前任务延时指定的 tick 数，期间任务处于挂起态
 * @note 需要开启 MRTK_USING_TIMER 配置宏
 * @param[in]  tick            延时的 tick 数（0 表示等同于 yield）
 * @retval MRTK_EOK    延时成功
 * @retval MRTK_ERROR 延时失败（在中断中调用）
 */
mrtk_err_t mrtk_task_delay(mrtk_tick_t tick);

/**
 * @brief 任务绝对延时（防止节拍漂移）
 * @details 延时到指定的绝对 tick 时刻，用于实现精确的周期性任务
 * @note 需要开启 MRTK_USING_TIMER 配置宏
 * @param[in]  last_wakeup     上次唤醒的 tick 指针
 * @param[in]  increment       增量的 tick 数
 * @retval MRTK_EOK    延时成功
 * @retval MRTK_ERROR 延时失败（在中断中调用）
 */
mrtk_err_t mrtk_task_delay_until(mrtk_tick_t *last_wakeup, mrtk_tick_t increment);

/* ==============================================================================
 * 属性控制 API (Attribute Control)
 * ============================================================================== */

/**
 * @brief 设置任务优先级
 * @details 修改任务优先级，如果任务处于就绪态，需要重新排序就绪队列
 * @param[in]  task            任务控制块指针
 * @param[in]  priority        新的优先级（0-31，0 最高）
 * @retval MRTK_EOK    设置成功
 * @retval MRTK_EINVAL 参数错误（task 为 MRTK_NULL 或 priority 超出范围）
 */
mrtk_err_t mrtk_task_set_priority(mrtk_task_t *task, mrtk_u8_t priority);

/**
 * @brief 获取任务优先级
 * @details 获取任务当前的优先级
 * @param[in]  task            任务控制块指针
 * @return mrtk_u8_t 任务优先级，失败返回 255
 */
mrtk_u8_t mrtk_task_get_priority(mrtk_task_t *task);

/* ==============================================================================
 * 通用控制接口 (Generic Control Interface)
 * ============================================================================== */

/**
 * @brief 任务通用控制接口
 * @details 支持多种控制命令，用于扩展任务管理功能
 * @param[in]  task            任务控制块指针
 * @param[in]  cmd             控制命令（见 mrtk_task_cmd_t）
 * @param[in]  arg             命令参数
 * @retval MRTK_EOK    控制成功
 * @retval MRTK_EINVAL 参数错误
 * @retval MRTK_ERROR 控制失败
 */
mrtk_err_t mrtk_task_control(mrtk_task_t *task, mrtk_u32_t cmd, mrtk_void_t *arg);

/* ==============================================================================
 * 辅助 API (Utility Functions)
 * ============================================================================== */

/**
 * @brief 获取当前运行任务的指针
 * @return mrtk_task_t* 当前运行任务的指针
 */
mrtk_task_t *mrtk_task_self(mrtk_void_t);

/**
 * @brief 启动内核调度器
 * @details 从此函数开始，内核接管控制权，开始任务调度
 * @note 永不返回
 */
mrtk_void_t mrtk_start(mrtk_void_t);

/* ==============================================================================
 * 内部 API (Internal API)
 * ============================================================================== */

/**
 * @brief 任务清理函数（底层私有接口）
 * @details 负责通用的逻辑清理，包括：
 *          - 停止内置定时器
 *          - 释放任务持有的所有互斥量
 *          - 从调度器移除（根据任务状态判断）
 *          - 修改任务状态为 CLOSE
 * @note 内部 API，请勿在应用代码中直接调用
 * @param[in] task 任务控制块指针
 */
mrtk_bool_t _mrtk_task_cleanup(mrtk_task_t *task);

/**
 * @brief 任务退出处理函数
 * @details 当任务入口函数返回时，硬件栈 LR 应指向此函数
 * @note 内部 API，用户不应直接调用
 * @note 调用 _mrtk_task_cleanup()，执行用户回调，挂入 g_defunct_task_list，触发调度
 */
mrtk_void_t _mrtk_task_exit(mrtk_void_t);

/* ==============================================================================
 * 对象状态导出 API (Object Dump API)
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出任务状态信息到控制台
 * @details 打印任务的名称、优先级、状态、栈信息、错误码等调试信息
 * @param[in]  task            任务控制块指针
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_task_dump(mrtk_task_t *task);

/**
 * @brief 导出系统中所有任务的统计信息
 * @details 遍历全局任务链表，打印所有任务的核心信息并输出统计汇总
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_task_dump_all(mrtk_void_t);

#endif /* (MRTK_DEBUG == 1) */

/* ==============================================================================
 * 空闲任务管理 API (Idle Task Management)
 * ============================================================================== */

/**
 * @brief 空闲任务入口函数
 * @details 空闲任务始终在系统中运行（最低优先级），负责：
 *          - 清理已删除任务（g_defunct_task_list）
 *          - 统计空闲时间
 *          - 可能的低功耗模式
 * @param[in] param 未使用（保留参数）
 * @note 内部 API，请勿在应用代码中直接调用
 */
mrtk_void_t mrtk_idle_task_entry(mrtk_void_t *param);

/**
 * @brief 获取空闲任务指针
 * @details 返回系统空闲任务的 TCB 指针
 * @return mrtk_task_t* 空闲任务指针
 */
mrtk_task_t *mrtk_task_get_idle(mrtk_void_t);

/**
 * @brief 初始化空闲任务
 * @details 创建并启动空闲任务，将其加入就绪队列
 * @note 内部 API，由系统初始化时调用
 * @retval MRTK_EOK   初始化成功
 * @retval MRTK_ERROR 初始化失败
 */
mrtk_err_t mrtk_task_init_idle(mrtk_void_t);

#if (MRTK_USING_TIMER_SOFT == 1)
/**
 * @brief 定时器守护任务入口函数
 * @details 持续监控软定时器链表，触发到期的软定时器回调
 * @note 工作机制：
 *       - 循环调用 mrtk_timer_soft_check() 扫描软定时器链表
 *       - 使用 mrtk_task_yield() 让出 CPU，允许其他任务运行
 *       - 如果软定时器链表为空或首个定时器未到期，快速返回
 *       - 通过 yield 而非 delay，既保证响应性又避免频繁挂起/恢复
 * @note 内部 API，由定时器守护任务调用
 * @param[in] param 任务参数（未使用）
 */
mrtk_void_t mrtk_timer_daemon_entry(mrtk_void_t *param);

/**
 * @brief 获取定时器守护任务指针
 * @details 返回系统定时器守护任务的 TCB 指针
 * @return mrtk_task_t* 定时器守护任务指针
 */
mrtk_task_t *mrtk_task_get_timer_daemon(mrtk_void_t);

/**
 * @brief 初始化定时器守护任务
 * @details 创建并启动定时器守护任务，将其加入就绪队列
 * @note 内部 API，由系统初始化时调用（需开启 MRTK_USING_TIMER_SOFT）
 * @note 守护任务优先级由 MRTK_TIMER_TASK_PRIO 配置（默认为 4）
 * @retval MRTK_EOK   初始化成功
 * @retval MRTK_ERROR 初始化失败
 */
mrtk_err_t mrtk_task_init_timer_daemon(mrtk_void_t);
#endif /* (MRTK_USING_TIMER_SOFT == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_TASK_H__ */
