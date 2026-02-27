/**
 * @file mrtk_obj.h
 * @author leiyx
 * @brief 内核对象基类定义
 * @details 定义内核对象的基类和类型枚举，所有内核对象（任务、信号量、互斥量等）都继承自此类
 * @copyright Copyright (c) 2026
 */

#ifndef __MRTK_OBJ_H__
#define __MRTK_OBJ_H__

#include "mrtk_config_internal.h"
#include "mrtk_list.h"
#include "mrtk_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * 内核对象类型常量
 * ============================================================================== */

/** 对象名称最大长度 */
#define MRTK_OBJ_NAME_MAX_LEN MRTK_NAME_MAX

/** 静态对象标志（对象在静态内存区） */
#define MRTK_OBJECT_TYPE_STATIC 0x00
/** 动态对象标志（对象在动态内存区） */
#define MRTK_OBJECT_TYPE_DYNAMIC 0x80

/**
 * @brief 内核对象类型枚举
 * @details 用于区分不同类型的内核对象，每个对象类型在全局对象链表数组中有独立索引
 */
typedef enum {
    MRTK_OBJ_TYPE_TASK     = 0x00, /**< 任务对象 */
    MRTK_OBJ_TYPE_MUTEX    = 0x01, /**< 互斥量对象 */
    MRTK_OBJ_TYPE_SEM      = 0x02, /**< 信号量对象 */
    MRTK_OBJ_TYPE_QUEUE    = 0x03, /**< 消息队列对象 */
    MRTK_OBJ_TYPE_MAIL     = 0x04, /**< 邮箱对象 */
    MRTK_OBJ_TYPE_EVENT    = 0x05, /**< 事件对象（暂未实现） */
    MRTK_OBJ_TYPE_MEM_POOL = 0x06, /**< 内存池对象 */
    MRTK_OBJ_TYPE_TIMER    = 0x07, /**< 定时器对象 */
    MRTK_OBJ_TYPE_DEVICE   = 0x08, /**< 设备对象（暂未实现） */
    MRTK_OBJ_TYPE_BUTT,            /**< 对象类型边界值（用于数组大小） */
} mrtk_obj_type;

/**
 * @brief 内存分配类型枚举
 * @details 指定内核对象的内存来源，影响对象创建和销毁的行为
 */
typedef enum {
    MRTK_ALLOC_TYPE_HEAP = 0x00, /**< 堆分配（适用于大小不固定的对象） */
    MRTK_ALLOC_TYPE_POOL = 0x01, /**< 内存池分配（适用于固定大小、高频创建销毁的对象） */
} mrtk_alloc_type;

/* ==============================================================================
 * 内核对象 Type 字段位掩码与操作宏
 * 设计说明：
 * - 低 4 位 (0~3) : 对象具体类型 (最多支持 16 种对象，见 mrtk_obj_type)
 * - 高 4 位 (4~7) : 对象的内存分配标志 (静态 0x00 / 动态 0x80)
 * ============================================================================== */

/* 1. 核心掩码定义 */
#define MRTK_OBJ_TYPE_MASK       0x0F /**< 对象类型掩码 (0000 1111) */
#define MRTK_OBJ_ALLOC_FLAG_MASK 0xF0 /**< 分配标志掩码 (1111 0000) */

/* 2. 获取（GET）宏：提取指定维度的信息 */
/** @brief 获取内核对象的具体类型 (返回 mrtk_obj_type 枚举值) */
#define MRTK_OBJ_GET_TYPE(type_val) ((type_val) & MRTK_OBJ_TYPE_MASK)

/** @brief 获取内核对象的分配标志 (返回 0x00 或 0x80) */
#define MRTK_OBJ_GET_ALLOC_FLAG(type_val) ((type_val) & MRTK_OBJ_ALLOC_FLAG_MASK)

/** @brief 判断对象是否为动态分配 */
#define MRTK_OBJ_IS_DYNAMIC(type_val)                                                              \
    (((type_val) & MRTK_OBJ_ALLOC_FLAG_MASK) == MRTK_OBJECT_TYPE_DYNAMIC)

/** @brief 判断对象是否为静态分配 */
#define MRTK_OBJ_IS_STATIC(type_val)                                                               \
    (((type_val) & MRTK_OBJ_ALLOC_FLAG_MASK) == MRTK_OBJECT_TYPE_STATIC)

/* 3. 设置（SET）宏：安全修改单一维度而不破坏另一个维度 */
/** @brief 将动态/静态标志和对象类型合并为一个字节（常用于初始化） */
#define MRTK_OBJ_BUILD_TYPE(alloc_flag, obj_type)                                                  \
    (mrtk_u8_t)(((alloc_flag) & MRTK_OBJ_ALLOC_FLAG_MASK) | ((obj_type) & MRTK_OBJ_TYPE_MASK))

/** @brief 修改对象类型，保持分配标志不变 */
#define MRTK_OBJ_SET_TYPE(type_var, obj_type)                                                      \
    do {                                                                                           \
        (type_var) = ((type_var) & MRTK_OBJ_ALLOC_FLAG_MASK) | ((obj_type) & MRTK_OBJ_TYPE_MASK);  \
    } while (0)

/** @brief 修改分配标志，保持对象类型不变 */
#define MRTK_OBJ_SET_ALLOC_FLAG(type_var, alloc_flag)                                              \
    do {                                                                                           \
        (type_var) =                                                                               \
            ((type_var) & MRTK_OBJ_TYPE_MASK) | ((alloc_flag) & MRTK_OBJ_ALLOC_FLAG_MASK);         \
    } while (0)

/**
 * @brief 内核对象基类结构体
 * @details 采用面向对象 C
 * 语言设计，所有内核对象（任务、信号量、互斥量等）都应将此结构体作为第一个成员
 * @note 通过继承此基类，所有内核对象都能挂载到全局对象链表中进行统一管理
 */
typedef struct mrtk_obj_def {
    mrtk_char_t      name[MRTK_OBJ_NAME_MAX_LEN]; /**< 对象名称 */
    mrtk_u8_t        type;                        /**< 对象类型（见 mrtk_obj_type 枚举） */
    mrtk_u8_t        flag;                        /**< 内存分配类型（静态或动态） */
    mrtk_list_node_t obj_node;                    /**< 全局对象链表节点 */
} mrtk_obj_t;

/** 全局内核对象链表头数组（按对象类型索引） */
extern mrtk_list_t g_obj_list[MRTK_OBJ_TYPE_BUTT];

/* ==============================================================================
 * 内核对象基类 API
 * ============================================================================== */

/**
 * @brief 内核对象基类构造函数
 * @details 初始化内核对象基类，将对象挂载到全局对象链表中
 * @param[in] obj   内核对象基类指针
 * @param[in] type  内核对象类型（见 mrtk_obj_type 枚举）
 * @param[in] flag  内存分配类型（MRTK_OBJECT_TYPE_STATIC 或 MRTK_OBJECT_TYPE_DYNAMIC）
 * @param[in] name  内核对象名称
 */
mrtk_void_t _mrtk_obj_init(mrtk_obj_t *obj, mrtk_u8_t type, mrtk_u8_t flag,
                           const mrtk_char_t *name);

/**
 * @brief 内核对象基类析构函数
 * @details 从全局对象链表中移除该对象
 * @param[in] obj 内核对象基类指针
 */
mrtk_void_t _mrtk_obj_delete(mrtk_void_t *obj);

/**
 * @brief 内核对象管理链表初始化
 * @note 用于系统初始化，无需关中断保护，
 */
mrtk_void_t _mrtk_obj_system_init(mrtk_void_t);

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_OBJ_H__ */