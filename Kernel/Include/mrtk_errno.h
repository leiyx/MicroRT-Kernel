/**
 * @file mrtk_errno.h
 * @author leiyx
 * @brief 内核错误码定义
 * @details 定义内核所有函数的返回值类型和错误码
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_ERRNO_H__
#define __MRTK_ERRNO_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 内核错误码枚举
 * @details 所有内核 API 函数的返回值均来自此枚举
 */
enum {
    MRTK_EOK      = 0, /**< 操作成功 */
    MRTK_ERROR    = 1, /**< 普通错误（未指定具体原因） */
    MRTK_EINVAL   = 2, /**< 非法参数（参数为 MRTK_NULL、超出范围等） */
    MRTK_EDELETED = 3, /**< 对象已被删除 */
    MRTK_EFULL    = 4, /**< 资源已满（队列满、缓冲区满等） */
    MRTK_EEMPTY   = 5, /**< 资源为空（队列空、信号量无资源等） */
    MRTK_E_IN_ISR = 6, /**< 不允许在中断中调用 */
    MRTK_EBUSY    = 7, /**< 资源忙碌中 */
    MRTK_ETIMEOUT = 8, /**< 操作超时（等待超时、定时器超时等） */
};

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_ERRNO_H__ */