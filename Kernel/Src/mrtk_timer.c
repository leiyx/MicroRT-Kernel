/**
 * @file mrtk_timer.c
 * @author leiyx
 * @brief 软件定时器实现
 * @details 提供单次和周期性定时器功能，支持硬定时器和软定时器两种模式
 * @note 使用有序链表组织定时器，按超时时间点升序排列，实现高效扫描
 * @copyright Copyright (c) 2026
 */

/* 必须首先包含配置文件以读取配置宏 */
#include "mrtk_config_internal.h"

#if (MRTK_USING_TIMER == 1)

#include "mrtk_errno.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_mem_heap.h"
#include "mrtk_obj.h"
#include "mrtk_timer.h"
#include "mrtk_typedef.h"
#if (MRTK_DEBUG == 1)
#include "mrtk_printf.h"
#endif

/* ==============================================================================
 * 全局变量定义
 * ============================================================================== */

/** 硬定时器链表头（在中断上下文中执行，按 timeout_tick 升序排列） */
mrtk_list_t g_hard_timer_list;

/** 软定时器链表头（在定时器任务上下文中执行，按 timeout_tick 升序排列） */
mrtk_list_t g_soft_timer_list;

/** 全局 Tick 计数器 */
volatile mrtk_u32_t g_mrtk_tick = 0;

/* ==============================================================================
 * 内部辅助函数
 * ============================================================================== */

/**
 * @brief 将定时器按超时时间插入有序链表
 * @details 遍历定时器链表，找到合适位置插入，保持链表按 timeout_tick 升序
 * @note 调用前必须关中断保护临界区，时间复杂度：O(n)，n 为定时器数量
 * @param[in] timer 待插入的定时器指针
 */
static mrtk_void_t _mrtk_timer_insert(mrtk_timer_t *timer)
{
    mrtk_timer_t *cur_timer;
    mrtk_list_t  *target_list;

    /*  根据定时器类型选择目标链表 */
    if (timer->obj.flag & MRTK_TIMER_FLAG_SOFT_TIMER) {
        target_list = &g_soft_timer_list;
    } else {
        target_list = &g_hard_timer_list;
    }

    /* 遍历链表找插入位置（找到第一个 timeout_tick > timer 的节点） */
    MRTK_LIST_FOR_EACH(cur_timer, target_list, mrtk_timer_t, timer_node)
    {
        if (cur_timer->timeout_tick > timer->timeout_tick) {
            _mrtk_list_insert_before(&cur_timer->timer_node, &timer->timer_node);
            return;
        }
    }

    /* 链表为空或 timer 的超时时间最大，插入到末尾 */
    _mrtk_list_insert_before(target_list, &timer->timer_node);

    /* 设置激活标志 */
    timer->obj.flag |= MRTK_TIMER_FLAG_ACTIVATED;
}

/**
 * @brief 从定时器链表中移除定时器
 * @note 调用前必须关中断保护临界区，时间复杂度：O(1)
 * @param[in] timer 待移除的定时器指针
 */
static inline mrtk_void_t _mrtk_timer_remove(mrtk_timer_t *timer)
{
    _mrtk_list_remove(&timer->timer_node);
    timer->obj.flag &= ~MRTK_TIMER_FLAG_ACTIVATED;
}

/* ==============================================================================
 * 公共 API 实现
 * ============================================================================== */

mrtk_void_t _mrtk_timer_system_init(mrtk_void_t)
{
    /* 初始化硬定时器链表和软定时器链表 */
    _mrtk_list_init(&g_hard_timer_list);
    _mrtk_list_init(&g_soft_timer_list);
    g_mrtk_tick = 0;
}

/* 在测试环境中，此函数由 mrtk_mock_hw.cc 提供 Mock 实现，使用条件编译避免符号重复定义 */
#ifndef UNIT_TESTING
mrtk_u32_t mrtk_tick_get(mrtk_void_t)
{
    return g_mrtk_tick;
    return 0;
}
#endif /* UNIT_TESTING */

mrtk_err_t mrtk_timer_init(mrtk_timer_t *timer, const mrtk_char_t *name,
                           mrtk_timer_timeout_func callback, mrtk_void_t *para, mrtk_u32_t tick,
                           mrtk_u8_t flag)
{
    if (timer == MRTK_NULL || name == MRTK_NULL || callback == MRTK_NULL) {
        return MRTK_EINVAL;
    }
    _mrtk_obj_init(&timer->obj, MRTK_OBJ_TYPE_TIMER | MRTK_OBJECT_TYPE_STATIC, flag, name);

    /* 清除激活标志，确保定时器处于未激活状态 */
    timer->obj.flag &= ~MRTK_TIMER_FLAG_ACTIVATED;

    /* 初始化定时器节点 */
    _mrtk_list_init(&timer->timer_node);

    /* 设置定时器参数 */
    timer->callback  = callback;
    timer->para      = para;
    timer->init_tick = tick;

    return MRTK_EOK;
}

mrtk_err_t mrtk_timer_detach(mrtk_timer_t *timer)
{
    if (timer == MRTK_NULL) {
        return MRTK_EINVAL;
    }
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 如果定时器已启动，直接从链表中移除 */
    if (timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) {
        _mrtk_timer_remove(timer);
    }

    /* 从全局对象链表中删除 */
    _mrtk_obj_delete(timer);

    mrtk_hw_interrupt_enable(level);

    return MRTK_EOK;
}

mrtk_timer_t *mrtk_timer_create(const mrtk_char_t *name, mrtk_timer_timeout_func callback,
                                mrtk_void_t *para, mrtk_u32_t tick, mrtk_u8_t flag)
{
    /* 从动态内存分配定时器对象 */
    mrtk_timer_t *timer = (mrtk_timer_t *) mrtk_malloc(sizeof(mrtk_timer_t));
    if (timer == MRTK_NULL) {
        return MRTK_NULL;
    }

    /* 初始化定时器 */
    mrtk_timer_init(timer, name, callback, para, tick, flag);

    /* 设置对象类型标志为动态分配 */
    MRTK_OBJ_SET_ALLOC_FLAG(timer->obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    return timer;
}

mrtk_err_t mrtk_timer_delete(mrtk_timer_t *timer)
{
    if (timer == MRTK_NULL) {
        return MRTK_EINVAL;
    }
    mrtk_ubase_t level = mrtk_hw_interrupt_disable();

    /* 如果定时器已启动，直接从链表中移除 */
    if (timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) {
        _mrtk_timer_remove(timer);
    }

    /* 从全局对象链表中删除 */
    _mrtk_obj_delete(timer);

    mrtk_hw_interrupt_enable(level);

    mrtk_free(timer);

    return MRTK_EOK;
}

mrtk_err_t mrtk_timer_start(mrtk_timer_t *timer)
{
    mrtk_base_t level;

    /* 软件定时器不支持永久等待 */
    if (timer == MRTK_NULL || timer->init_tick == MRTK_WAITING_FOREVER) {
        return MRTK_EINVAL;
    }

    level = mrtk_hw_interrupt_disable();

    /* 如果定时器已启动，先移除再重新插入（支持重启定时器） */
    if (timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) {
        _mrtk_timer_remove(timer);
    }

    /* 计算超时时间点（当前 Tick + 设定时间） */
    timer->timeout_tick = mrtk_tick_get() + timer->init_tick;

    /* 将定时器插入全局定时链表 */
    _mrtk_timer_insert(timer);

    mrtk_hw_interrupt_enable(level);

    return MRTK_EOK;
}

mrtk_err_t mrtk_timer_stop(mrtk_timer_t *timer)
{
    mrtk_base_t level;

    if (timer == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    level = mrtk_hw_interrupt_disable();

    /* 若定时器未启动 */
    if (!(timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED)) {
        mrtk_hw_interrupt_enable(level);
        return MRTK_ERROR;
    }

    _mrtk_timer_remove(timer);

    mrtk_hw_interrupt_enable(level);

    return MRTK_EOK;
}

mrtk_err_t mrtk_timer_control(mrtk_timer_t *timer, mrtk_u32_t cmd, mrtk_void_t *arg)
{
    mrtk_base_t level;
    mrtk_err_t  ret = MRTK_EOK;

    if (timer == MRTK_NULL) {
        return MRTK_EINVAL;
    }

    level = mrtk_hw_interrupt_disable();

    /* 命令分发 */
    switch (cmd) {
    case MRTK_TIMER_CMD_SET_TIME:
        /* 修改定时时间，周期定时器会在下一个周期使用新值 */
        if (arg == MRTK_NULL) {
            ret = MRTK_EINVAL;
            break;
        }
        timer->init_tick = *(mrtk_u32_t *) arg;
        break;

    case MRTK_TIMER_CMD_GET_TIME:
        /* 获取定时时间 */
        if (arg == MRTK_NULL) {
            ret = MRTK_EINVAL;
            break;
        }
        *(mrtk_u32_t *) arg = timer->init_tick;
        break;

    case MRTK_TIMER_CMD_SET_ONESHOT:
        /* 设置为单次模式（清除周期标志） */
        timer->obj.flag &= ~MRTK_TIMER_FLAG_PERIODIC;
        break;

    case MRTK_TIMER_CMD_SET_PERIODIC:
        /* 设置为周期模式 */
        timer->obj.flag |= MRTK_TIMER_FLAG_PERIODIC;
        break;

    case MRTK_TIMER_CMD_SET_HARD_MODE:
    case MRTK_TIMER_CMD_SET_SOFT_MODE: {
        /* 如果定时器正在运行，需要先从当前链表移除，再插入到正确的链表 */
        mrtk_u8_t was_activated = (timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED);

        if (was_activated) {
            /* 从当前链表移除 */
            _mrtk_timer_remove(timer);
        }

        /* 修改模式标志 */
        if (cmd == MRTK_TIMER_CMD_SET_SOFT_MODE) {
            timer->obj.flag |= MRTK_TIMER_FLAG_SOFT_TIMER;
        } else {
            timer->obj.flag &= ~MRTK_TIMER_FLAG_SOFT_TIMER;
        }

        /* 如果之前是激活状态，重新插入到正确的链表 */
        if (was_activated) {
            _mrtk_timer_insert(timer);
        }
        break;
    }

    default:
        /* 不支持的命令 */
        ret = MRTK_EINVAL;
        break;
    }

    mrtk_hw_interrupt_enable(level);

    return ret;
}

mrtk_void_t mrtk_timer_hard_check(mrtk_void_t)
{
    mrtk_timer_t *first_timer;
    mrtk_base_t   level    = mrtk_hw_interrupt_disable();
    mrtk_u32_t    cur_tick = mrtk_tick_get();

    /* 只要链表不为空，就不断检查链表的第一个节点 */
    while (!_mrtk_list_is_empty(&g_hard_timer_list)) {
        first_timer = _mrtk_list_entry(g_hard_timer_list.next, mrtk_timer_t, timer_node);

        /*  如果头部定时器未到期，说明后面的全都没到期（定时器升序排列），直接退出扫描 */
        if (!(cur_tick - first_timer->timeout_tick < MRTK_TICK_MAX / 2)) {
            break;
        }

        /* 已经到期，将其从链表彻底摘除，并清除激活标志 */
        _mrtk_timer_remove(first_timer);

        /* 开中断，执行回调 */
        mrtk_hw_interrupt_enable(level);
        first_timer->callback(first_timer->para);
        level = mrtk_hw_interrupt_disable();

        /* 回调执行完毕重新关中断。如果是周期定时器，重新计算并插入 */
        if (first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC) {
            /* 累加重启，防止时间漂移 */
            first_timer->timeout_tick += first_timer->init_tick;
            _mrtk_timer_insert(first_timer);
        }
    }

    mrtk_hw_interrupt_enable(level);
}

/* 定时任务的入口函数 */
mrtk_void_t mrtk_timer_soft_check(mrtk_void_t)
{
    mrtk_timer_t *first_timer;
    mrtk_base_t   level    = mrtk_hw_interrupt_disable();
    mrtk_u32_t    cur_tick = mrtk_tick_get();

    /* 只要链表不为空，就不断检查链表的第一个节点 */
    while (!_mrtk_list_is_empty(&g_soft_timer_list)) {
        first_timer = _mrtk_list_entry(g_soft_timer_list.next, mrtk_timer_t, timer_node);

        /*  如果头部定时器未到期，说明后面的全都没到期（定时器升序排列），直接退出扫描 */
        if (!(cur_tick - first_timer->timeout_tick < MRTK_TICK_MAX / 2)) {
            break;
        }

        /* 已经到期，将其从链表彻底摘除，并清除激活标志 */
        _mrtk_timer_remove(first_timer);

        /* 恢复中断，准备在定时任务上下文中执行回调（允许阻塞） */
        mrtk_hw_interrupt_enable(level);

        /* 调用回调函数 */
        first_timer->callback(first_timer->para);

        level = mrtk_hw_interrupt_disable();

        /* 回调执行完毕重新关中断。如果是周期定时器，重新计算并插入 */
        if (first_timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC) {
            /* 累加重启，防止时间漂移 */
            first_timer->timeout_tick += first_timer->init_tick;
            _mrtk_timer_insert(first_timer);
        }
    }

    mrtk_hw_interrupt_enable(level);
}

#if (MRTK_DEBUG == 1)

/* 定时器类型字符串映射表 */
static const mrtk_char_t *g_timer_type_str[] = {
    "HARD", /**< 硬定时器（中断上下文执行） */
    "SOFT", /**< 软定时器（任务上下文执行） */
};

/* 定时器模式字符串映射表 */
static const mrtk_char_t *g_timer_mode_str[] = {
    "ONE_SHOT", /**< 单次模式 */
    "PERIODIC", /**< 周期模式 */
};

/**
 * @brief 导出定时器状态信息到控制台
 * @details 打印定时器的名称、工作模式、激活状态、超时时间等调试信息
 * @note 内部 API，请勿在应用代码中直接调用
 * @param[in] timer 定时器控制块指针
 */
mrtk_void_t mrtk_timer_dump(mrtk_timer_t *timer)
{
    /* 防御性编程：检查空指针 */
    if (timer == MRTK_NULL) {
        mrtk_printf("Dump Error: MRTK_NULL pointer\r\n");
        return;
    }

    /* 从 obj.flag 获取定时器类型和模式 */
    mrtk_bool_t is_soft = (timer->obj.flag & MRTK_TIMER_FLAG_SOFT_TIMER) ? MRTK_TRUE : MRTK_FALSE;
    mrtk_bool_t is_periodic = (timer->obj.flag & MRTK_TIMER_FLAG_PERIODIC) ? MRTK_TRUE : MRTK_FALSE;

    const mrtk_char_t *type_str = is_soft ? g_timer_type_str[1] : g_timer_type_str[0];
    const mrtk_char_t *mode_str = is_periodic ? g_timer_mode_str[1] : g_timer_mode_str[0];

    /* 获取激活状态 */
    mrtk_bool_t is_activated =
        (timer->obj.flag & MRTK_TIMER_FLAG_ACTIVATED) ? MRTK_TRUE : MRTK_FALSE;
    const mrtk_char_t *state_str = is_activated ? "ACTIVATED" : "DEACTIVATED";

    /* 获取当前系统 Tick */
    mrtk_u32_t current_tick = mrtk_tick_get();

    /* 计算剩余 Tick 数（仅对已激活定时器有效） */
    mrtk_s32_t remain_tick = 0;
    if (is_activated) {
        remain_tick = (mrtk_s32_t) (timer->timeout_tick - current_tick);
    }

    /* 输出对象基类信息 */
    mrtk_printf(
        "================================================================================\r\n");
    mrtk_printf("[Timer Object Dump]\r\n");
    mrtk_printf("  Name        : %s\r\n", timer->obj.name);
    mrtk_printf("  Type        : TIMER\r\n");
    mrtk_printf("  Address     : 0x%p\r\n", timer);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出工作模式和状态 */
    mrtk_printf("  Timer Type  : %s\r\n", type_str);
    mrtk_printf("  Timer Mode  : %s\r\n", mode_str);
    mrtk_printf("  State       : %s\r\n", state_str);
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出时间信息 */
    mrtk_printf("  InitTick    : %u ticks\r\n", timer->init_tick);
    mrtk_printf("  TimeoutTick : %u ticks (absolute)\r\n", timer->timeout_tick);
    mrtk_printf("  CurrentTick : %u ticks\r\n", current_tick);
    if (is_activated) {
        mrtk_printf("  RemainTick  : %d ticks\r\n", remain_tick);
    } else {
        mrtk_printf("  RemainTick  : N/A (timer not activated)\r\n");
    }
    mrtk_printf(
        "--------------------------------------------------------------------------------\r\n");

    /* 输出回调信息 */
    mrtk_printf("  Callback    : 0x%p\r\n", timer->callback);
    mrtk_printf("  Parameter   : 0x%p\r\n", timer->para);
    mrtk_printf(
        "================================================================================\r\n");
}

#endif /* (MRTK_DEBUG == 1) */

#endif /* (MRTK_USING_TIMER == 1) */
