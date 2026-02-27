/**
 * @file mrtk_printf.h
 * @author leiyx
 * @brief 内核格式化输出接口（对标 FreeRTOS printf/RT-Thread rt_kprintf）
 * @details 提供轻量级的格式化输出功能，通过 Port 层接口实现硬件无关的输出
 * @note 内核层只负责格式化，硬件输出由 Port 层 mrtk_hw_output_string() 实现
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_PRINTF_H__
#define __MRTK_PRINTF_H__

#include "mrtk_config_internal.h"

#if (MRTK_DEBUG == 1)

#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 内核 printf 输出缓冲区大小
 * @details 单次格式化输出的最大长度
 * @note 可在 mrtk_config.h 中配置
 */
#ifndef MRTK_PRINTF_BUF_SIZE
#define MRTK_PRINTF_BUF_SIZE 256
#endif

/* ==============================================================================
 * 内核 printf API
 * ============================================================================== */

/**
 * @brief 内核格式化输出函数
 * @details 支持 printf 标准格式化字符串，用于内核调试输出
 * @param format 格式化字符串
 * @param ... 可变参数列表
 * @return mrtk_s32_t 输出的字符数，失败返回负值
 *
 * @note
 *      - 使用 <stdarg.h> 的 va_list 和 vsnprintf 实现变长参数处理
 *      - 格式化后的字符串通过 Port 层的 mrtk_hw_output_string() 输出
 *      - 内核不关心输出设备（串口/USB/屏幕），由 Port 层决定
 *
 * @code
 *      // 使用示例
 *      mrtk_printf("System init done!\r\n");
 *      mrtk_printf("Value: %d, Address: 0x%p\r\n", value, ptr);
 * @endcode
 */
mrtk_s32_t mrtk_printf(const mrtk_char_t *format, ...);

#else /* (MRTK_DEBUG == 0) */

/* printf 禁用时，编译为内联空函数（零开销） */
static inline mrtk_s32_t mrtk_printf(const mrtk_char_t *format, ...)
{
    (void) format;
    return 0;
}

#endif /* MRTK_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_PRINTF_H__ */
