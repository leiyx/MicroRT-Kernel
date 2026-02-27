/**
 * @file mrtk_typedef.h
 * @author leiyx
 * @brief 内核基础类型定义和工具宏
 * @details 提供统一的数据类型重定义、位操作宏、数学工具宏、编译器属性等，
 *          用于增强代码的可移植性和可维护性
 * @note 此文件必须在所有内核头文件之前包含（除 mrtk_config.h 外）
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_TYPEDEF_H__
#define __MRTK_TYPEDEF_H__

/* 内部配置文件 - 包含用户配置、默认值补充和依赖校验 */
#include "mrtk_config_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 基础标量类型重定义
 * ============================================================================== */

/**
 * @defgroup basic_types 基础数据类型
 * @{
 */

/** 64 位无符号整数 */
typedef uint64_t mrtk_u64_t;
/** 64 位有符号整数 */
typedef int64_t mrtk_s64_t;
/** 32 位无符号整数 */
typedef uint32_t mrtk_u32_t;
/** 32 位有符号整数 */
typedef int32_t mrtk_s32_t;
/** 16 位无符号整数 */
typedef uint16_t mrtk_u16_t;
/** 16 位有符号整数 */
typedef int16_t mrtk_s16_t;
/** 8 位无符号整数 */
typedef uint8_t mrtk_u8_t;
/** 8 位有符号整数 */
typedef int8_t mrtk_s8_t;

/** void 类型重定义 */
typedef void mrtk_void_t;
/** 布尔类型 */
typedef uint8_t mrtk_bool_t;
/** 字符类型 */
typedef char mrtk_char_t;
/** 指针长度的无符号整数类型 */
typedef uintptr_t mrtk_ptr_t;
/** 用于表示指针偏移的有符号类型 */
typedef intptr_t mrtk_off_t;
/** CPU 的字长类型（32 位系统是 32 bit，64 位系统是 64 bit） */
typedef unsigned int mrtk_size_t;

/** 用于表示一个寄存器宽度的有符号整数 */
typedef long mrtk_base_t;
/** 用于表示一个寄存器宽度的无符号整数 */
typedef unsigned long mrtk_ubase_t;

/** @} */

/* ==============================================================================
 * RTOS 特有类型
 * ============================================================================== */

/**
 * @defgroup rtos_types RTOS 特有类型
 * @{
 */

/** 系统滴答计数类型 */
typedef uint32_t mrtk_tick_t;
/** 时间（毫秒）类型 */
typedef uint32_t mrtk_time_t;
/** 内核错误码类型 */
typedef int32_t mrtk_err_t;
/** 任务入口函数指针类型 */
typedef mrtk_void_t (*mrtk_task_entry_t)(mrtk_void_t *para);
/** 清理回调函数指针类型 */
typedef mrtk_void_t (*mrtk_cleanup_handler_t)(mrtk_void_t *para);

/** @} */

/* ==============================================================================
 * 布尔常量
 * ============================================================================== */

/**
 * @defgroup bool_constants 布尔常量
 * @{
 */

/** 真 */
#define MRTK_TRUE 1
/** 假 */
#define MRTK_FALSE 0

/** @} */

/* ==============================================================================
 * 空指针常量
 * ============================================================================== */

/** 空指针 */
#define MRTK_NULL ((void *) 0)

/* ==============================================================================
 * 类型极值常量
 * ============================================================================== */

/**
 * @defgroup type_limits 类型极值常量
 * @{
 */

/** uint32_t 最大值 */
#define MRTK_U32_MAX (0xFFFFFFFFU)
/** uint16_t 最大值 */
#define MRTK_U16_MAX (0xFFFFU)
/** uint8_t 最大值 */
#define MRTK_U8_MAX (0xFFU)

/** int32_t 最大值 */
#define MRTK_S32_MAX (0x7FFFFFFFL)
/** int16_t 最大值 */
#define MRTK_S16_MAX (0x7FFFL)
/** int8_t 最大值 */
#define MRTK_S8_MAX (0x7FL)

/** int32_t 最小值 */
#define MRTK_S32_MIN (-0x7FFFFFFFL - 1)
/** int16_t 最小值 */
#define MRTK_S16_MIN (-0x7FFFL - 1)
/** int8_t 最小值 */
#define MRTK_S8_MIN (-0x7FL - 1)

/** @} */

/* ==============================================================================
 * 无效值常量
 * ============================================================================== */

/**
 * @defgroup invalid_values 无效值常量
 * @details 用于表示无效或未初始化的值
 * @{
 */

/** 无效 uint8_t 值 */
#define MRTK_INVALID_U8 (0xFFU)
/** 无效 uint16_t 值 */
#define MRTK_INVALID_U16 (0xFFFFU)
/** 无效 uint32_t 值 */
#define MRTK_INVALID_U32 (0xFFFFFFFFU)

/** @} */

/* ==============================================================================
 * 时间和等待常量
 * ============================================================================== */

/**
 * @defgroup time_constants 时间和等待常量
 * @{
 */

/** 无限等待时间（用于信号量、消息队列等阻塞函数） */
#define MRTK_WAIT_FOREVER 0xFFFFFFFFUL

/** 不等待（立即返回） */
#define MRTK_NO_WAIT 0UL

#define MRTK_UINT32_MAX 0xffffffff

#define MRTK_TICK_MAX MRTK_UINT32_MAX

/** @} */

/* ==============================================================================
 * 优先级常量
 * ============================================================================== */

/**
 * @defgroup priority_constants 优先级常量
 * @{
 */

/** 最大优先级（数值越小优先级越高） */
#define MRTK_MAX_PRIO 0

/** 最小优先级（空闲任务优先级） */
#define MRTK_MIN_PRIO 31

/** 优先级级别总数 */
#define MRTK_PRIO_LEVEL_NUM 32

/** @} */

/* 工具宏和函数已移动到 mrtk_utils.h，如需使用请包含该头文件 */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_TYPEDEF_H__ */
