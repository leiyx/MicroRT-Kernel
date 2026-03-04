/**
 * @file mrtk_timer.h
 * @author leiyx
 * @brief 软件定时器管理模块接口定义
 * @details 提供单次和周期定时器功能，支持硬定时器（中断中执行）和软定时器（任务中执行）
 * @note 使用有序链表组织定时器，按超时时间点升序排列，实现高效扫描
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_TIMER_H__
#define __MRTK_TIMER_H__

#include "mrtk_config_internal.h"
#include "mrtk_errno.h"
#include "mrtk_obj.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MRTK_WAITING_FOREVER -1

/* ==============================================================================
 * 时间单位转换宏 (Time Unit Conversion Macros)
 * ============================================================================== */

/**
 * @brief 将 Tick 转换为毫秒
 * @param tick Tick 数值
 * @return 对应的毫秒数
 * @note 计算公式：(tick) * 1000 / MRTK_TICK_PER_SECOND
 */
#define MRTK_TICK_TO_MS(tick) ((tick) * 1000UL / MRTK_TICK_PER_SECOND)

/**
 * @brief 将毫秒转换为 Tick（四舍五入）
 * @param ms 毫秒数值
 * @return 对应的 Tick 数值
 * @note 计算公式：(ms) * MRTK_TICK_PER_SECOND / 1000
 */
#define MRTK_MS_TO_TICK(ms) ((ms) * MRTK_TICK_PER_SECOND / 1000UL)

/**
 * @brief 将毫秒转换为 Tick（向下取整）
 * @param ms 毫秒数值
 * @return 对应的 Tick 数值（向下取整）
 * @note 适用于需要确保等待时间不超过指定毫秒数的场景
 */
#define MRTK_MS_TO_TICK_FLOOR(ms) ((ms) * MRTK_TICK_PER_SECOND / 1000UL)

/**
 * @brief 将毫秒转换为 Tick（向上取整）
 * @param ms 毫秒数值
 * @return 对应的 Tick 数值（向上取整）
 * @note 适用于需要确保等待时间至少达到指定毫秒数的场景
 */
#define MRTK_MS_TO_TICK_CEIL(ms) (((ms) * MRTK_TICK_PER_SECOND + 1000UL - 1) / 1000UL)

/**
 * @brief 将 Tick 转换为秒
 * @param tick Tick 数值
 * @return 对应的秒数
 * @note 计算公式：(tick) / MRTK_TICK_PER_SECOND
 */
#define MRTK_TICK_TO_SEC(tick) ((tick) / MRTK_TICK_PER_SECOND)

/**
 * @brief 将秒转换为 Tick
 * @param sec 秒数值
 * @return 对应的 Tick 数值
 * @note 计算公式：(sec) * MRTK_TICK_PER_SECOND
 */
#define MRTK_SEC_TO_TICK(sec) ((sec) * MRTK_TICK_PER_SECOND)

/**
 * @brief 将秒转换为毫秒
 * @param sec 秒数值
 * @return 对应的毫秒数
 * @note 计算公式：(sec) * 1000
 */
#define MRTK_SEC_TO_MS(sec) ((sec) * 1000UL)

/**
 * @brief 将毫秒转换为秒（向下取整）
 * @param ms 毫秒数值
 * @return 对应的秒数（向下取整）
 * @note 计算公式：(ms) / 1000
 */
#define MRTK_MS_TO_SEC(ms) ((ms) / 1000UL)

/* ==============================================================================
 * 定时器标志位定义
 * ============================================================================== */

/** 定时器处于未激活状态 */
#define MRTK_TIMER_FLAG_DEACTIVATED 0x0
/** 定时器已启动并在内核链表中维护 */
#define MRTK_TIMER_FLAG_ACTIVATED 0x1
/** 单次定时模式（超时后自动停止） */
#define MRTK_TIMER_FLAG_ONE_SHOT 0x0
/** 周期定时模式（超时后自动重启） */
#define MRTK_TIMER_FLAG_PERIODIC 0x2
/** 硬定时器（在中断上下文中执行回调） */
#define MRTK_TIMER_FLAG_HARD_TIMER 0x0
/** 软定时器（在定时器任务上下文中执行回调） */
#define MRTK_TIMER_FLAG_SOFT_TIMER 0x4

/* ==============================================================================
 * 定时器控制命令枚举
 * ============================================================================== */

/**
 * @brief 定时器控制命令枚举
 * @details 用于 mrtk_timer_control() 函数的命令参数
 */
typedef enum {
    MRTK_TIMER_CMD_SET_TIME      = 0x01, /**< 设置定时器超时时间（arg: mrtk_u32_t*） */
    MRTK_TIMER_CMD_GET_TIME      = 0x02, /**< 获取定时器初始时间（arg: mrtk_u32_t*） */
    MRTK_TIMER_CMD_SET_ONESHOT   = 0x04, /**< 设置为单次模式（arg: MRTK_NULL） */
    MRTK_TIMER_CMD_SET_PERIODIC  = 0x05, /**< 设置为周期模式（arg: MRTK_NULL） */
    MRTK_TIMER_CMD_SET_HARD_MODE = 0x06, /**< 修改为硬定时器模式（arg: MRTK_NULL） */
    MRTK_TIMER_CMD_SET_SOFT_MODE = 0x07  /**< 修改为软定时器模式（arg: MRTK_NULL） */
} mrtk_timer_cmd_t;

/* ==============================================================================
 * 全局变量声明
 * ============================================================================== */

/** 硬定时器链表头（在中断上下文中执行） */
extern mrtk_list_t g_hard_timer_list;

/** 软定时器链表头（在定时器任务上下文中执行） */
extern mrtk_list_t g_soft_timer_list;

/**
 * @brief 全局 Tick 计数器
 * @details 由 SysTick 中断周期性递增，用于时间管理、超时判断和轮转调度
 * @note 32 位无符号整数，约 49.7 天回卷一次（@ 1kHz tick）
 */
extern volatile mrtk_u32_t g_mrtk_tick;

/* ==============================================================================
 * 定时器类型定义
 * ============================================================================== */

/** 定时器超时回调函数类型 */
typedef mrtk_void_t (*mrtk_timer_timeout_func)(mrtk_void_t *);

/**
 * @brief 定时器控制块结构体
 * @details 管理软件定时器的所有信息
 * @note 定时器按 timeout_tick 升序挂在全局链表中，实现高效扫描
 */
typedef struct mrtk_timer_def {
    mrtk_obj_t  obj;        /**< 内核对象基类 */
    mrtk_list_t timer_node; /**< 定时器链表节点（用于挂载到系统定时器链表） */

    mrtk_timer_timeout_func callback; /**< 超时回调函数指针 */
    mrtk_void_t            *para;     /**< 超时回调函数参数 */

    mrtk_u32_t init_tick;    /**< 设定的超时时间（Tick） */
    mrtk_u32_t timeout_tick; /**< 下次触发超时的绝对时间点（当前 tick + init_tick） */
} mrtk_timer_t;

/* ==============================================================================
 * 定时器管理 API
 * ============================================================================== */

/**
 * @brief 定时器模块初始化
 * @details 初始化全局定时器链表，系统启动时调用
 * @note 内部 API，由内核系统初始化代码调用
 */
mrtk_void_t _mrtk_timer_system_init(mrtk_void_t);

/**
 * @brief 定时器静态初始化
 * @param[out] timer    定时器控制块指针
 * @param[in]  name     定时器名称
 * @param[in]  callback 超时回调函数指针
 * @param[in]  para     回调函数参数
 * @param[in]  tick     超时时间（单位为 Tick）
 * @param[in]  flag     定时模式标志（单次/周期 + 硬定时器/软定时器）
 */
mrtk_err_t mrtk_timer_init(mrtk_timer_t *timer, const mrtk_char_t *name,
                           mrtk_timer_timeout_func callback, mrtk_void_t *para, mrtk_u32_t tick,
                           mrtk_u8_t flag);

/**
 * @brief 定时器静态脱离
 * @param[in] timer 定时器控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_ERROR 定时器停止失败
 */
mrtk_err_t mrtk_timer_detach(mrtk_timer_t *timer);

/**
 * @brief 定时器动态创建
 * @param[in] name     定时器名称
 * @param[in] callback 超时回调函数指针
 * @param[in] para     回调函数参数
 * @param[in] tick     超时时间（单位为 Tick）
 * @param[in] flag     定时模式标志（单次/周期 + 硬定时器/软定时器）
 * @return mrtk_timer_t* 成功返回定时器指针，失败返回 MRTK_NULL
 */
mrtk_timer_t *mrtk_timer_create(const mrtk_char_t *name, mrtk_timer_timeout_func callback,
                                mrtk_void_t *para, mrtk_u32_t tick, mrtk_u8_t flag);

/**
 * @brief 定时器动态删除
 * @details 停止定时器并释放内存
 * @param[in] timer 定时器控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_timer_delete(mrtk_timer_t *timer);

/**
 * @brief 启动定时器
 * @details 将定时器加入系统定时器链表，开始计时
 * @note 时间复杂度：O(n)，n 为定时器数量
 * @param[in] timer 定时器控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_timer_start(mrtk_timer_t *timer);

/**
 * @brief 停止定时器
 * @details 将定时器从系统定时器链表中移除
 * @note 时间复杂度：O(1)
 * @param[in] timer 定时器控制块指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_ERROR 定时器未启动
 * @retval MRTK_EINVAL 参数错误
 */
mrtk_err_t mrtk_timer_stop(mrtk_timer_t *timer);

/**
 * @brief 获取或设置定时器属性
 * @warning 修改运行中周期定时器的超时时间会在下一个周期生效
 * @param[in]     timer 定时器控制块指针
 * @param[in]     cmd   控制命令（见 mrtk_timer_cmd_t 枚举）
 * @param[in,out] arg   命令参数指针
 * @retval MRTK_EOK   成功
 * @retval MRTK_EINVAL 参数错误或命令不支持
 */
mrtk_err_t mrtk_timer_control(mrtk_timer_t *timer, mrtk_u32_t cmd, mrtk_void_t *arg);

/**
 * @brief 定时器检查函数（硬定时器处理）
 * @details 在 SysTick 中断中调用，扫描并触发到期的硬定时器
 * @note 仅在中断上下文中调用，由内核自动调用，应用代码不应直接调用
 * @warning 执行时间与到期硬定时器数量成正比，应避免在回调中执行耗时操作
 * @warning 回调执行期间中断已打开，用户代码中修改定时器链表需格外谨慎
 */
mrtk_void_t mrtk_timer_hard_check(mrtk_void_t);

/**
 * @brief 定时器检查函数（软定时器处理）
 * @details 作为定时器守护任务的入口函数，扫描并触发到期的软定时器
 * @note 仅在任务上下文中调用，由定时器守护任务自动调用
 * @warning 执行时间与到期软定时器数量成正比
 * @note 软定时器允许在回调中执行阻塞操作或耗时操作
 */
mrtk_void_t mrtk_timer_soft_check(mrtk_void_t);

/* ==============================================================================
 * Tick 计数器管理 API (Tick Counter Management)
 * ============================================================================== */

/**
 * @brief 获取当前系统 Tick 计数
 * @details 返回自系统启动以来的 Tick 计数，线程安全
 * @return mrtk_u32_t 当前 Tick 值
 */
mrtk_u32_t mrtk_tick_get(mrtk_void_t);

/* ==============================================================================
 * 对象状态导出 API (Object Dump API)
 * ============================================================================== */

#if (MRTK_DEBUG == 1)

/**
 * @brief 导出定时器状态信息到控制台
 * @details 打印定时器的名称、工作模式、激活状态、超时时间等调试信息
 * @param[in] timer 定时器控制块指针
 * @note 需要开启 MRTK_DEBUG 配置宏
 */
mrtk_void_t mrtk_timer_dump(mrtk_timer_t *timer);

#endif /* (MRTK_DEBUG == 1) */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_TIMER_H__ */
