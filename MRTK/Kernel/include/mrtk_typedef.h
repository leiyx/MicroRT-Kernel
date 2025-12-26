/**
 * @file mrtk_typedef.h
 * @author leiyx
 * @brief 内核基础类型定义
 * @version 0.1
 * @copyright Copyright (c) 2025
 */

#ifndef MRTK_TYPEDEF_H
#define MRTK_TYPEDEF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 基础标量类型重定义 --- */
typedef uint64_t mrtk_u64_t;
typedef int64_t  mrtk_i64_t;
typedef uint32_t mrtk_u32_t;
typedef int32_t  mrtk_i32_t;
typedef uint16_t mrtk_u16_t;
typedef int16_t  mrtk_i16_t;
typedef uint8_t  mrtk_u8_t;
typedef int8_t   mrtk_i8_t;

typedef void      mrtk_void_t;
typedef uint8_t   mrtk_bool_t;
typedef char      mrtk_char_t;
typedef uintptr_t mrtk_ptr_t; /**< 指针长度的无符号整数类型 */
typedef intptr_t  mrtk_off_t; /**< 用于表示指针偏移的有符号类型 */

/* --- RTOS 特有类型 --- */
typedef uint32_t mrtk_tick_t; /**< 系统滴答计数类型 */
typedef uint32_t mrtk_time_t; /**< 时间（毫秒）类型 */
typedef int32_t  mrtk_err_t;  /**< 内核错误码类型 */

/* --- 常量定义 --- */

#define MRTK_TRUE  1
#define MRTK_FALSE 0
#define MRTK_NULL  ((void *) 0)

#define MRTK_U32_MAX (0xFFFFFFFFU)
#define MRTK_U16_MAX (0xFFFFU)
#define MRTK_U8_MAX  (0xFFU)

#endif // MRTK_TYPEDEF_H