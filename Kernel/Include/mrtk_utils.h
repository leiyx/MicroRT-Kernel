/**
 * @file mrtk_utils.h
 * @author leiyx
 * @brief 内核工具宏和内联函数
 * @details 提供位操作、数学工具、对齐、编译器属性、断言、内存操作等工具宏
 * @note 此文件应在需要使用工具宏时包含
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_UTILS_H__
#define __MRTK_UTILS_H__

/* 引入基础类型定义 */
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 容器辅助宏
 * ============================================================================== */

/**
 * @defgroup container_macros 容器辅助宏
 * @{
 */

/**
 * @brief 获取成员在结构体中的偏移量
 * @details 利用空指针 (type *)0 来计算成员偏移
 * @param type 结构体类型
 * @param member 成员名称
 * @return size_t 成员偏移量（字节）
 */
#define MRTK_OFFSET_OF(type, member) ((size_t) &((type *) 0)->member)

/**
 * @brief 根据结构体成员指针获取包含它的结构体指针
 * @details 通过成员指针减去成员偏移量得到结构体起始地址
 * @note 这是侵入式链表和面向对象 C 设计的核心宏
 * @param ptr 成员指针
 * @param type 结构体类型
 * @param member 成员名称
 * @return type* 结构体指针
 */
#define MRTK_CONTAINER_OF(ptr, type, member)                                                       \
    ((type *) ((char *) (ptr) - MRTK_OFFSET_OF(type, member)))

/** @} */

/* ==============================================================================
 * 位操作宏
 * ============================================================================== */

/**
 * @defgroup bit_macros 位操作宏
 * @details 提供常用的位操作，所有参数都应避免副作用
 * @{
 */

/**
 * @brief 生成第 n 位的掩码
 * @details 例如：MRTK_BIT(3) -> 0x08
 * @note 使用 1U 防止有符号左移导致的未定义行为
 * @param n 位编号（从 0 开始）
 * @return 第 n 位为 1 的掩码值
 */
#define MRTK_BIT(n) (1U << (n))

/**
 * @brief 生成多位的掩码
 * @details 例如：MRTK_BITS(3, 0) -> 0x0F (第 0-3 位为 1)
 * @note 注意参数顺序：高位在前，低位在后
 * @param high 高位编号
 * @param low 低位编号
 * @return 从 low 到 high 位都为 1 的掩码值
 */
#define MRTK_BITS(high, low) ((((~0UL) << (low)) & (~0UL >> (31 - (high)))) << 0)

/**
 * @brief 判断第 n 位是否为 1
 * @param val 要检查的值
 * @param n 位编号（从 0 开始）
 * @return non-zero (真) 或 0 (假)
 */
#define MRTK_IS_BIT_SET(val, n) (((val) & MRTK_BIT(n)) != 0U)

/**
 * @brief 判断第 n 位是否为 0
 * @param val 要检查的值
 * @param n 位编号（从 0 开始）
 * @return non-zero (真) 或 0 (假)
 */
#define MRTK_IS_BIT_CLR(val, n) (((val) & MRTK_BIT(n)) == 0U)

/**
 * @brief 获取第 n 位的值（返回 0 或 1）
 * @details 使用了 !! 双重取反技巧归一化为 0 或 1
 * @param val 要提取的值
 * @param n 位编号（从 0 开始）
 * @return 0 或 1
 */
#define MRTK_GET_BIT_VAL(val, n) (!!((val) & MRTK_BIT(n)))

/**
 * @brief 将第 n 位置 1
 * @param var 目标变量（必须是左值）
 * @param n 位编号（从 0 开始）
 */
#define MRTK_SET_BIT(var, n) ((var) |= MRTK_BIT(n))

/**
 * @brief 将第 n 位清 0
 * @param var 目标变量（必须是左值）
 * @param n 位编号（从 0 开始）
 */
#define MRTK_CLR_BIT(var, n) ((var) &= ~MRTK_BIT(n))

/**
 * @brief 翻转第 n 位（0->1, 1->0）
 * @param var 目标变量（必须是左值）
 * @param n 位编号（从 0 开始）
 */
#define MRTK_TOGGLE_BIT(var, n) ((var) ^= MRTK_BIT(n))

/**
 * @brief 设置多个位的值
 * @param var 目标变量（必须是左值）
 * @param mask 位掩码
 * @param val 要设置的值（只取 mask 中为 1 的位）
 */
#define MRTK_MODIFY_BITS(var, mask, val) ((var) = ((var) & ~(mask)) | ((val) & (mask)))

/** @} */

/* ==============================================================================
 * 数学工具宏
 * ============================================================================== */

/**
 * @defgroup math_macros 数学工具宏
 * @details 提供常用的数学运算宏，所有操作都应避免副作用
 * @{
 */

/**
 * @brief 获取两个数的最小值
 * @param x 第一个数
 * @param y 第二个数
 * @return 较小的值
 */
#define MRTK_MIN(x, y) ((x) < (y) ? (x) : (y))

/**
 * @brief 获取两个数的最大值
 * @param x 第一个数
 * @param y 第二个数
 * @return 较大的值
 */
#define MRTK_MAX(x, y) ((x) > (y) ? (x) : (y))

/**
 * @brief 限制值在指定范围内
 * @param val 要限制的值
 * @param min 最小值
 * @param max 最大值
 * @return 限制后的值（min <= result <= max）
 */
#define MRTK_CLAMP(val, min, max) MRTK_MIN(MRTK_MAX(val, min), max)

/**
 * @brief 取绝对值
 * @param x 要计算绝对值的数
 * @return 绝对值
 */
#define MRTK_ABS(x) ((x) < 0 ? -(x) : (x))

/**
 * @brief 向上取整除法
 * @details 计算 (x + d - 1) / d，用于确保结果向上取整
 * @param x 被除数
 * @param d 除数（必须大于 0）
 * @return 向上取整的结果
 */
#define MRTK_DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))

/**
 * @brief 四舍五入除法
 * @details 计算 (x + d/2) / d，用于四舍五入
 * @param x 被除数
 * @param d 除数
 * @return 四舍五入的结果
 */
#define MRTK_DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))

/**
 * @brief 交换两个变量的值
 * @details 使用临时变量交换，支持任意类型
 * @note 使用 do-while(0) 确保作为单条语句使用时安全
 * @param a 第一个变量（必须是左值）
 * @param b 第二个变量（必须是左值）
 */
#define MRTK_SWAP(a, b)                                                                            \
    do {                                                                                           \
        typeof(a) _tmp = (a);                                                                      \
        (a)            = (b);                                                                      \
        (b)            = _tmp;                                                                     \
    } while (0)

/**
 * @brief 获取数组元素个数
 * @param array 数组名称
 * @return 数组元素个数
 */
#define MRTK_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/** @} */

/* ==============================================================================
 * 对齐宏
 * ============================================================================== */

/**
 * @defgroup align_macros 对齐宏
 * @{
 */

/**
 * @brief 向下对齐
 * @details 将 size 向下对齐到 align 的整数倍
 * @note align 必须是 2 的幂
 * @param size 要对齐的值
 * @param align 对齐边界（必须是 2 的幂）
 * @return 对齐后的值
 */
#define MRTK_ALIGN_DOWN(size, align) ((size) & ~((align) - 1))

/**
 * @brief 向上对齐
 * @details 将 size 向上对齐到 align 的整数倍
 * @note align 必须是 2 的幂
 * @param size 要对齐的值
 * @param align 对齐边界（必须是 2 的幂）
 * @return 对齐后的值
 */
#define MRTK_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

/**
 * @brief 检查是否对齐
 * @details 检查 value 是否按 align 对齐
 * @param value 要检查的值
 * @param align 对齐边界（必须是 2 的幂）
 * @return 0（未对齐）或非 0（已对齐）
 */
#define MRTK_IS_ALIGNED(value, align) (((value) & ((align) - 1)) == 0)

/** @} */

/* ==============================================================================
 * 编译器属性宏
 * ============================================================================== */

/**
 * @defgroup compiler_attributes 编译器属性宏
 * @details 提供跨编译器的属性定义
 * @{
 */

/**
 * @brief 结构体按 1 字节对齐（去除填充）
 * @note 使用示例：struct MRTK_PACKED mrtk_struct { ... };
 */
#if defined(__CC_ARM)
#define MRTK_PACKED __packed
#elif defined(__ICCARM__) || defined(__GNUC__)
#define MRTK_PACKED __attribute__((packed))
#else
#define MRTK_PACKED
#endif

/**
 * @brief 指定结构体或变量的对齐方式
 * @note 使用示例：mrtk_u8_t data[16] MRTK_ALIGNED(16);
 */
#if defined(__CC_ARM)
#define MRTK_ALIGNED(x) __align(x)
#elif defined(__ICCARM__) || defined(__GNUC__)
#define MRTK_ALIGNED(x) __attribute__((aligned(x)))
#else
#define MRTK_ALIGNED(x)
#endif

/**
 * @brief 声明弱符号
 * @details 允许用户覆盖默认实现
 * @note 使用示例：void mrtk_hook(void) MRTK_WEAK;
 */
#if defined(__CC_ARM)
#define MRTK_WEAK __weak
#elif defined(__ICCARM__) || defined(__GNUC__)
#define MRTK_WEAK __attribute__((weak))
#else
#define MRTK_WEAK
#endif

/**
 * @brief 标记变量或函数可能未使用
 * @details 告诉编译器不要产生"未使用"警告
 * @note 使用示例：static void unused_func(void) MRTK_UNUSED;
 */
#if defined(__CC_ARM)
#define MRTK_UNUSED __attribute__((unused))
#elif defined(__ICCARM__) || defined(__GNUC__)
#define MRTK_UNUSED __attribute__((unused))
#else
#define MRTK_UNUSED
#endif

/**
 * @brief 标记参数未使用
 * @details 用于函数参数，消除"未使用参数"警告
 * @note 使用示例：void func(int arg MRTK_PARAM_UNUSED);
 */
#define MRTK_PARAM_UNUSED MRTK_UNUSED

/**
 * @brief 强制符号被链接到目标文件
 * @details 防止链接器优化掉看似未使用的变量或函数
 */
#if defined(__GNUC__)
#define MRTK_USED __attribute__((used))
#else
#define MRTK_USED
#endif

/** @} */

/* ==============================================================================
 * 断言和调试宏
 * ============================================================================== */

/**
 * @defgroup debug_macros 断言和调试宏
 * @{
 */

/**
 * @brief 断言宏
 * @details 在调试模式下检查条件，如果条件为假则进入死循环
 * @note 生产环境中编译为空语句，无运行时开销
 * @param EX 要检查的表达式
 */
#if (MRTK_USING_ASSERT == 1)
#define MRTK_ASSERT(EX)                                                                            \
    do {                                                                                           \
        if (!(EX)) {                                                                               \
            while (1) {                                                                            \
                ; /* 可以在这里添加断点或打印 */                                                   \
            }                                                                                      \
        }                                                                                          \
    } while (0)
#else
#define MRTK_ASSERT(EX) ((void) 0)
#endif

/**
 * @brief 静态断言（编译时检查）
 * @details 在编译时检查条件，如果为假则编译失败
 * @note 使用示例：MRTK_STATIC_ASSERT(sizeof(int) == 4, "int must be 32-bit");
 */
#if defined(__CC_ARM)
#define MRTK_STATIC_ASSERT(expr, msg) extern const char MRTK_STATIC_ASSERT_##msg[(expr) ? 1 : -1]
#elif defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 6)
#define MRTK_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#else
#define MRTK_STATIC_ASSERT(expr, msg) extern const char MRTK_STATIC_ASSERT_##msg[(expr) ? 1 : -1]
#endif

/** @} */

/* ==============================================================================
 * 内存操作宏
 * ============================================================================== */

/**
 * @defgroup memory_macros 内存操作宏
 * @{
 */

/**
 * @brief 编译器内存屏障
 * @details 防止编译器重新排序内存访问
 * @note 用于确保关键内存操作的顺序
 */
#if defined(__GNUC__) || defined(__CC_ARM)
#define MRTK_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define MRTK_BARRIER()
#endif

/**
 * @brief 数据同步屏障（DSB）
 * @details ARM 架构专用，确保所有内存访问完成
 */
#if defined(__GNUC__) || defined(__CC_ARM)
#define MRTK_DSB() __asm__ __volatile__("dsb" ::: "memory")
#else
#define MRTK_DSB()
#endif

/** @} */

/* ==============================================================================
 * 字节序转换宏
 * ============================================================================== */

/**
 * @defgroup endian_macros 字节序转换宏
 * @details 用于网络字节序和主机字节序之间的转换
 * @note ARM Cortex-M 是小端序，网络序是大端序
 * @{
 */

/**
 * @brief 16 位大小端交换
 * @param val 16 位值
 * @return 字节序交换后的值
 */
#define MRTK_SWAP16(val) ((uint16_t) (((val) << 8) | ((val) >> 8)))

/**
 * @brief 32 位大小端交换
 * @param val 32 位值
 * @return 字节序交换后的值
 */
#define MRTK_SWAP32(val)                                                                           \
    ((uint32_t) (((val) << 24) | (((val) & 0x0000FF00UL) << 8) | (((val) & 0x00FF0000UL) >> 8) |   \
                 ((val) >> 24)))

/**
 * @brief 主机序转网络序（16 位）
 * @param val 16 位主机序值
 * @return 网络序值
 */
#define MRTK_HTONS(val) MRTK_SWAP16(val)

/**
 * @brief 网络序转主机序（16 位）
 * @param val 16 位网络序值
 * @return 主机序值
 */
#define MRTK_NTOHS(val) MRTK_SWAP16(val)

/**
 * @brief 主机序转网络序（32 位）
 * @param val 32 位主机序值
 * @return 网络序值
 */
#define MRTK_HTONL(val) MRTK_SWAP32(val)

/**
 * @brief 网络序转主机序（32 位）
 * @param val 32 位网络序值
 * @return 主机序值
 */
#define MRTK_NTOHL(val) MRTK_SWAP32(val)

/** @} */

/* ==============================================================================
 * 便捷宏
 * ============================================================================== */

/**
 * @defgroup utility_macros 便捷宏
 * @{
 */

/**
 * @brief 获取数组的字节数
 * @param array 数组名称
 */
#define MRTK_ARRAY_BYTES(array) (sizeof(array))

/**
 * @brief 将值限制在 8 位无符号整数范围内
 */
#define MRTK_U8(x) ((mrtk_u8_t) (x))

/**
 * @brief 将值限制在 16 位无符号整数范围内
 */
#define MRTK_U16(x) ((mrtk_u16_t) (x))

/**
 * @brief 将值限制在 32 位无符号整数范围内
 */
#define MRTK_U32(x) ((mrtk_u32_t) (x))

/**
 * @brief 将值限制在有符号 8 位整数范围内
 */
#define MRTK_S8(x) ((mrtk_s8_t) (x))

/**
 * @brief 将值限制在有符号 16 位整数范围内
 */
#define MRTK_S16(x) ((mrtk_s16_t) (x))

/**
 * @brief 将值限制在有符号 32 位整数范围内
 */
#define MRTK_S32(x) ((mrtk_s32_t) (x))

/** @} */

/* ==============================================================================
 * 字符串工具函数
 * ============================================================================== */

/**
 * @defgroup string_functions 字符串工具函数
 * @details 提供安全的字符串操作函数（参考 RT-Thread 实现）
 * @{
 */

/**
 * @brief 安全的字符串复制函数（参考 RT-Thread rt_strncpy）
 * @details 与标准 strncpy 不同，此函数会显式填充剩余字节为 0，避免垃圾数据
 * @param dst 目标字符串地址
 * @param src 源字符串地址
 * @param n 最大复制长度（字节）
 * @return 返回目标字符串地址
 * @note
 *      - 如果源字符串长度小于 n，剩余字节填充为 0
 *      - 如果源字符串长度大于等于 n，复制 n 字节（不保证 null 终止）
 *      - 调用者应确保 dst 有足够空间（至少 n 字节）
 */
static inline mrtk_char_t *mrtk_strncpy(mrtk_char_t *dst, const mrtk_char_t *src, mrtk_size_t n)
{
    if (n != 0) {
        mrtk_char_t       *d = dst;
        const mrtk_char_t *s = src;

        do {
            /* 当赋值表达式的值 (*s) 为 0（即遇到空字符），
             * 且 n 未耗尽时，会用 0 填充剩余的 dst 空间，
             * 避免复制 RAM 中的垃圾数据 */
            if ((*d++ = *s++) == '\0') {
                /* NUL 填充剩余的 n-1 字节 */
                while (--n != 0) {
                    *d++ = '\0';
                }
                break;
            }
        } while (--n != 0);
    }

    return dst;
}

/**
 * @brief 计算字符串长度
 * @param s 字符串指针
 * @return 字符串长度（不包括 null 终止符）
 */
static inline mrtk_size_t mrtk_strlen(const mrtk_char_t *s)
{
    const mrtk_char_t *p = s;
    while (*p != '\0') {
        p++;
    }
    return (mrtk_size_t) (p - s);
}

/**
 * @brief 字符串比较
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 0（相等）、<0（s1 < s2）、>0（s1 > s2）
 */
static inline int mrtk_strcmp(const mrtk_char_t *s1, const mrtk_char_t *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int) (*(mrtk_u8_t *) s1 - *(mrtk_u8_t *) s2);
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_UTILS_H__ */
