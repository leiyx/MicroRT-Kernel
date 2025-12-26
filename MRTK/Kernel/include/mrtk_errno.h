/**
 * @file mrtk_errno.h
 * @author leiyx
 * @brief 内核错误码定义
 * @version 0.1
 * @copyright Copyright (c) 2025
 */

#ifndef MRTK_ERRNO_H
#define MRTK_ERRNO_H

/**
 * @brief 内核错误码枚举
 */
enum {
    MRTK_EOK    = 0, /**< 操作成功 */
    MRTK_ERROR  = 1, /**< 普通错误 */
    MRTK_EINVAL = 2, /**< 非法参数 */
};

#endif // MRTK_ERRNO_H