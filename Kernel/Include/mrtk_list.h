/**
 * @file mrtk_list.h
 * @author leiyx
 * @brief 侵入式双向循环链表实现
 * @details 所有操作均为 O(1) 时间复杂度，采用内联函数实现
 * @note 非线程安全，多线程环境需配合调度锁使用
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_LIST_H__
#define __MRTK_LIST_H__

#include "mrtk_typedef.h"
#include "mrtk_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 侵入式链表节点结构体定义
 * @details 采用侵入式设计，不包含数据域，需要嵌入到用户自定义结构体中使用。
 * 类型说明：两者类型完全相同，仅用于语义区分
 * - `mrtk_list_t`：用于表示链表头节点（哨兵节点）
 * - `mrtk_list_node_t`：用于表示链表的元素节点
 * @note 侵入式链表的核心思想是将链表节点嵌入到数据结构中，
 *       而不是包含数据结构。这样可以在不知道数据结构大小的情况下操作链表
 */
typedef struct mrtk_list_node_def {
    struct mrtk_list_node_def *prev; /**< 指向前一个节点的指针 */
    struct mrtk_list_node_def *next; /**< 指向后一个节点的指针 */
} mrtk_list_t, mrtk_list_node_t;

/* ==============================================================================
 * 链表遍历宏
 * ============================================================================== */

/**
 * @defgroup list_macros 链表遍历宏
 * @details 简化侵入式链表的遍历代码
 * @{
 */

/**
 * @brief 遍历链表（带节点类型）
 * @details 用于简化侵入式链表的遍历代码
 * @param node 当前节点指针（循环变量）
 * @param head 链表头指针
 * @param type 包含节点的结构体类型
 * @param member 节点成员名称
 */
#define MRTK_LIST_FOR_EACH(node, head, type, member)                                               \
    for ((node) = MRTK_CONTAINER_OF((head)->next, type, member); &(node)->member != (head);        \
         (node) = MRTK_CONTAINER_OF((node)->member.next, type, member))

/**
 * @brief 安全遍历链表（允许删除当前节点）
 * @details 在删除节点时也能安全继续遍历
 * @param node 当前节点指针（循环变量）
 * @param _next 下一个节点临时变量
 * @param head 链表头指针
 * @param type 包含节点的结构体类型
 * @param member 节点成员名称
 */
#define MRTK_LIST_FOR_EACH_SAFE(node, _next, head, type, member)                                   \
    for ((node) = MRTK_CONTAINER_OF((head)->next, type, member),                                   \
        (_next) = MRTK_CONTAINER_OF((node)->member.next, type, member);                            \
         &(node)->member != (head);                                                                \
         (node) = (_next), (_next) = MRTK_CONTAINER_OF((node)->member.next, type, member))

/** @} */

/**
 * @brief 初始化链表
 * @note 使用链表前必须先初始化
 * @param[in] head_node 链表头节点指针
 */
static inline mrtk_void_t _mrtk_list_init(mrtk_list_t *head_node)
{
    head_node->next = head_node;
    head_node->prev = head_node;
}

/**
 * @brief 获取链表长度（不包括头节点）
 * @details 通过遍历链表计算节点数量，时间复杂度 O(n)
 * @note 频繁调用此函数会影响性能，建议在需要时缓存长度
 * @param[in] head_node 链表头节点指针
 * @return mrtk_u32_t 链表中的节点数量
 */
static inline mrtk_u32_t _mrtk_list_len(mrtk_list_t *head_node)
{
    mrtk_u32_t        len = 0;
    mrtk_list_node_t *cur = head_node;

    while (cur->next != head_node) {
        ++len;
        cur = cur->next;
    }
    return len;
}

/**
 * @brief 通过链表节点获取宿主结构体的指针
 * @param node 链表节点的指针
 * @param type 宿主结构体的类型 (如 mrtk_task_t)
 * @param member 链表节点在宿主结构体中的成员名称
 * @note 依赖底层 MRTK_CONTAINER_OF 宏支持
 */
#define _mrtk_list_entry(node, type, member) MRTK_CONTAINER_OF(node, type, member)

/**
 * @brief 判断链表是否为空
 * @param[in] head_node 链表头节点指针
 * @retval MRTK_TRUE 链表为空
 * @retval MRTK_FALSE 链表非空
 */
static inline mrtk_bool_t _mrtk_list_is_empty(mrtk_list_t *head_node)
{
    return (head_node->next == head_node) ? MRTK_TRUE : MRTK_FALSE;
}

/**
 * @brief 插入目标节点到参考节点之后
 * @param[in] node 参考节点指针
 * @param[in] insert_node 要插入的目标节点指针
 */
static inline mrtk_void_t _mrtk_list_insert_after(mrtk_list_node_t *node,
                                                  mrtk_list_node_t *insert_node)
{
    insert_node->next = node->next;
    insert_node->prev = node;
    node->next->prev  = insert_node;
    node->next        = insert_node;
}

/**
 * @brief 插入目标节点到参考节点之前
 * @param[in] node 参考节点指针
 * @param[in] insert_node 要插入的目标节点指针
 */
static inline mrtk_void_t _mrtk_list_insert_before(mrtk_list_node_t *node,
                                                   mrtk_list_node_t *insert_node)
{
    _mrtk_list_insert_after(node->prev, insert_node);
}

/**
 * @brief 从链表中移除目标节点
 * @details 将 remove_node 从链表中移除，并使其前后指针指向自己
 * @param[in] remove_node 要移除的目标节点指针
 */
static inline mrtk_void_t _mrtk_list_remove(mrtk_list_node_t *remove_node)
{
    remove_node->prev->next = remove_node->next;
    remove_node->next->prev = remove_node->prev;
    remove_node->next       = remove_node;
    remove_node->prev       = remove_node;
}

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_LIST_H__ */
