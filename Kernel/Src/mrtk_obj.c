/**
 * @file mrtk_obj.c
 * @author leiyx
 * @brief 内核对象基类实现
 * @copyright Copyright (c) 2026
 */

#include "mrtk_obj.h"
#include "mrtk_irq.h"
#include "mrtk_list.h"
#include "mrtk_schedule.h"
#include "mrtk_typedef.h"
#include "mrtk_utils.h"

/* ==============================================================================
 * 全局变量定义
 * ============================================================================== */

/** 全局内核对象链表头数组（按对象类型索引） */
mrtk_list_t g_obj_list[MRTK_OBJ_TYPE_BUTT];

/* ==============================================================================
 * API 实现
 * ============================================================================== */

/* 需外部调用者负责关中断 */
mrtk_void_t _mrtk_obj_init(mrtk_obj_t *obj, mrtk_u8_t type, mrtk_u8_t flag, const mrtk_char_t *name)
{
    /* 复制对象名称，会自动填充剩余字节为 0，避免垃圾数据 */
    mrtk_strncpy(obj->name, name, MRTK_OBJ_NAME_MAX_LEN);
    /* 当传入name长度大于MRTK_OBJ_NAME_MAX_LEN，自动截断 */
    obj->name[MRTK_OBJ_NAME_MAX_LEN - 1] = '\0';

    /* 设置对象类型和标志 */
    obj->type = type;
    obj->flag = flag;

    mrtk_u8_t obj_type = MRTK_OBJ_GET_TYPE(obj->type);

    /* 将对象插入到全局对象链表的头部 */
    _mrtk_list_insert_after(&g_obj_list[obj_type], &obj->obj_node);
}

/* 需外部调用者负责关中断 */
mrtk_void_t _mrtk_obj_delete(mrtk_void_t *obj)
{
    _mrtk_list_remove(&((mrtk_obj_t *) obj)->obj_node);
}

mrtk_void_t _mrtk_obj_system_init(mrtk_void_t)
{
    /* 初始化全局对象链表数组 */
    for (mrtk_u32_t i = 0; i < (mrtk_u32_t) MRTK_OBJ_TYPE_BUTT; ++i) {
        _mrtk_list_init(&g_obj_list[i]);
    }
}