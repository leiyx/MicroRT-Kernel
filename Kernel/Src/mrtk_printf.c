/**
 * @file mrtk_printf.c
 * @author leiyx
 * @brief 内核格式化输出实现
 * @details 使用 vsnprintf 进行格式化，通过 Port 层接口输出
 * @copyright Copyright (c) 2026
 */

#include "mrtk_printf.h"

#if (MRTK_DEBUG == 1)

#include "cpu_port.h"
#include <stdarg.h>
#include <stdio.h>

/* ==============================================================================
 * 内部配置
 * ============================================================================== */

/**
 * @brief 格式化输出缓冲区
 * @details 存储 vsnprintf 格式化后的结果
 */
static mrtk_char_t g_printf_buffer[MRTK_PRINTF_BUF_SIZE];

/* ==============================================================================
 * 公共 API 实现
 * ============================================================================== */

/**
 * @brief 内核格式化输出函数
 * @param format 格式化字符串
 * @param ... 可变参数列表
 * @return mrtk_s32_t 输出的字符数，失败返回负值
 *
 * @note 实现原理：
 *      1. 使用 va_start 获取可变参数列表
 *      2. 使用 vsnprintf 将格式化结果写入缓冲区
 *      3. 调用 Port 层的 mrtk_hw_output_string() 输出
 *      4. 使用 va_end 清理可变参数列表
 */
mrtk_s32_t mrtk_printf(const mrtk_char_t *format, ...)
{
    mrtk_s32_t len = 0;
    va_list    args;

    /* 参数检查 */
    if (format == MRTK_NULL) {
        return -1;
    }

    /* 1. 获取可变参数列表 */
    va_start(args, format);

    /* 2. 格式化字符串到缓冲区 */
    len = vsnprintf(g_printf_buffer, MRTK_PRINTF_BUF_SIZE, format, args);

    /* 3. 调用 Port 层接口输出 */
    if (len > 0) {
        mrtk_hw_output_string(g_printf_buffer);
    }

    /* 4. 清理可变参数列表 */
    va_end(args);

    return len;
}

#endif /* MRTK_DEBUG */
