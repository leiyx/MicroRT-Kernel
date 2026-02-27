/**
 * @file mrtk_test_list.cc
 * @author leiyx
 * @brief 侵入式双向循环链表单元测试
 * @details 完整的测试覆盖，包括边界值分析、等价类划分、分支覆盖和状态机覆盖
 *
 * 测试覆盖策略：
 * 1. 边界值分析：空链表、单节点、双节点、大量节点(1000)
 * 2. 等价类划分：正常操作、边界位置插入/删除（头、尾、中间）
 * 3. 分支覆盖：while循环、三元运算符、所有指针操作路径
 * 4. 状态机覆盖：Init -> Insert -> Remove -> Clear 完整生命周期
 * 5. 宏测试：MRTK_LIST_FOR_EACH 和 MRTK_LIST_FOR_EACH_SAFE
 * 6. 容器宏测试：_mrtk_list_entry (MRTK_CONTAINER_OF)
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/* MRTK 内核头文件 */
#include "mrtk_list.h"
#include "mrtk_typedef.h"
#include "mrtk_utils.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"

/**
 * @brief 测试任务结构体（模拟TCB）
 * @details 使用侵入式链表节点嵌入到结构体中
 */
typedef struct test_task_def {
    mrtk_u32_t        task_id;          /**< 任务ID */
    mrtk_u8_t         priority;         /**< 优先级 */
    mrtk_list_node_t  list_node;        /**< 侵入式链表节点 */
} test_task_t;

/**
 * @class MrtkListTest
 * @brief 双向链表测试夹具
 * @details 提供测试前的环境准备和测试后的环境清理
 */
class MrtkListTest : public ::testing::Test {
  protected:
    mrtk_list_t test_list;              /**< 测试链表头 */
    test_task_t  tasks[10];             /**< 测试任务数组 */
    mrtk_u32_t   task_counter;          /**< 任务计数器 */

    /**
     * @brief 测试前设置
     * @details 初始化测试环境，确保每个测试用例都在干净的环境中运行
     */
    void SetUp() override {
        /* 复位全局变量 */
        for (int i = 0; i < MRTK_OBJ_TYPE_BUTT; i++) {
            _mrtk_list_init(&g_obj_list[i]);
        }
        _mrtk_list_init(&g_defunct_task_list);
        for (int i = 0; i < MRTK_MAX_PRIO_LEVEL_NUM; i++) {
            _mrtk_list_init(&g_ready_task_list[i]);
        }

        /* 初始化测试链表 */
        _mrtk_list_init(&test_list);

        /* 初始化任务计数器 */
        task_counter = 0;

        /* 初始化所有测试任务 */
        for (int i = 0; i < 10; i++) {
            tasks[i].task_id  = 0;
            tasks[i].priority = 0;
            _mrtk_list_init(&tasks[i].list_node);
        }
    }

    /**
     * @brief 测试后清理
     * @details 执行必要的清理工作
     */
    void TearDown() override {
        /* GTest 会自动检查内存泄漏 */
    }

    /**
     * @brief 创建一个新任务
     * @param task_id 任务ID
     * @param priority 优先级
     * @return test_task_t* 任务指针
     */
    test_task_t* create_task(mrtk_u32_t task_id, mrtk_u8_t priority) {
        if (task_counter >= 10) {
            return nullptr;
        }

        test_task_t* task = &tasks[task_counter++];
        task->task_id  = task_id;
        task->priority = priority;
        _mrtk_list_init(&task->list_node);

        return task;
    }

    /**
     * @brief 验证链表完整性
     * @details 检查链表的prev和next指针是否正确连接
     * @param expected_len 期望的链表长度
     */
    void verify_list_integrity(mrtk_u32_t expected_len) {
        /* Step 1: 验证长度 */
        EXPECT_EQ(_mrtk_list_len(&test_list), expected_len);

        /* Step 2: 验证链表完整性（双向连接） */
        mrtk_u32_t        len = 0;
        mrtk_list_node_t *cur = test_list.next;

        while (cur != &test_list) {
            /* 验证双向连接：cur->prev->next == cur */
            EXPECT_EQ(cur->prev->next, cur);

            /* 验证双向连接：cur->next->prev == cur */
            EXPECT_EQ(cur->next->prev, cur);

            len++;
            cur = cur->next;

            /* 防止死循环（安全检查） */
            ASSERT_LE(len, expected_len + 1);
        }

        EXPECT_EQ(len, expected_len);

        /* Step 3: 验证头节点的连接 */
        if (expected_len == 0) {
            EXPECT_EQ(test_list.next, &test_list);
            EXPECT_EQ(test_list.prev, &test_list);
        } else {
            /* 头节点的next指向第一个元素 */
            EXPECT_NE(test_list.next, &test_list);

            /* 头节点的prev指向最后一个元素 */
            EXPECT_NE(test_list.prev, &test_list);

            /* 最后一个元素的next指向头节点 */
            EXPECT_EQ(test_list.prev->next, &test_list);

            /* 第一个元素的prev指向头节点 */
            EXPECT_EQ(test_list.next->prev, &test_list);
        }
    }
};

/* ==============================================================================
 * 初始化测试
 * ============================================================================== */

/**
 * @test 初始化空链表
 * @details 验证初始化后的链表头节点指向自己，长度为0
 * @covers _mrtk_list_init
 */
TEST_F(MrtkListTest, Init_EmptyList_PointToSelf) {
    /* Step 1: Given - 链表已初始化 */

    /* Step 2: When - 执行初始化（已在 SetUp 中完成） */

    /* Step 3: Then - 验证头节点指向自己 */
    EXPECT_EQ(test_list.next, &test_list);
    EXPECT_EQ(test_list.prev, &test_list);

    /* Step 4: Then - 验证链表为空 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);

    /* Step 5: Then - 验证链表长度为0 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(0);
}

/* ==============================================================================
 * 插入测试
 * ============================================================================== */

/**
 * @test 在空链表中插入单个节点（头插法）
 * @details 验证在空链表头节点后插入第一个节点后的状态
 * @covers _mrtk_list_insert_after
 */
TEST_F(MrtkListTest, InsertAfter_SingleNode_EmptyList) {
    /* Step 1: Given - 空链表 + 新节点 */
    test_task_t* task = create_task(1, 10);
    ASSERT_NE(task, nullptr);

    /* Step 2: When - 在头节点后插入 */
    _mrtk_list_insert_after(&test_list, &task->list_node);

    /* Step 3: Then - 验证链表长度为1 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 1);

    /* Step 4: Then - 验证链表非空 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_FALSE);

    /* Step 5: Then - 验证头节点连接 */
    EXPECT_EQ(test_list.next, &task->list_node);
    EXPECT_EQ(test_list.prev, &task->list_node);

    /* Step 6: Then - 验证节点连接回自身（循环链表） */
    EXPECT_EQ(task->list_node.next, &test_list);
    EXPECT_EQ(task->list_node.prev, &test_list);

    /* Step 7: Then - 验证链表完整性 */
    verify_list_integrity(1);
}

/**
 * @test 在链表尾部插入节点
 * @details 验证在已有节点后继续插入节点
 * @covers _mrtk_list_insert_after
 */
TEST_F(MrtkListTest, InsertAfter_AppendToEnd_MultipleNodes) {
    /* Step 1: Given - 空链表 + 3个任务 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    /* Step 2: When - 依次插入3个节点 */
    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 3: Then - 验证链表长度为3 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 3);

    /* Step 4: Then - 验证节点顺序：head -> task1 -> task2 -> task3 -> head */
    EXPECT_EQ(test_list.next, &task1->list_node);
    EXPECT_EQ(task1->list_node.next, &task2->list_node);
    EXPECT_EQ(task2->list_node.next, &task3->list_node);
    EXPECT_EQ(task3->list_node.next, &test_list);

    /* Step 5: Then - 验证反向指针 */
    EXPECT_EQ(test_list.prev, &task3->list_node);
    EXPECT_EQ(task3->list_node.prev, &task2->list_node);
    EXPECT_EQ(task2->list_node.prev, &task1->list_node);
    EXPECT_EQ(task1->list_node.prev, &test_list);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(3);
}

/**
 * @test 在中间位置插入节点
 * @details 验证在两个已有节点之间插入新节点
 * @covers _mrtk_list_insert_after
 */
TEST_F(MrtkListTest, InsertAfter_InMiddle_BetweenTwoNodes) {
    /* Step 1: Given - 2个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task_middle = create_task(99, 15);  /* 中间插入的任务 */
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task_middle, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);

    /* Step 2: When - 在task1和task2之间插入task_middle */
    _mrtk_list_insert_after(&task1->list_node, &task_middle->list_node);

    /* Step 3: Then - 验证链表长度为3 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 3);

    /* Step 4: Then - 验证节点顺序：head -> task1 -> task_middle -> task2 -> head */
    EXPECT_EQ(test_list.next, &task1->list_node);
    EXPECT_EQ(task1->list_node.next, &task_middle->list_node);
    EXPECT_EQ(task_middle->list_node.next, &task2->list_node);
    EXPECT_EQ(task2->list_node.next, &test_list);

    /* Step 5: Then - 验证反向指针 */
    EXPECT_EQ(task2->list_node.prev, &task_middle->list_node);
    EXPECT_EQ(task_middle->list_node.prev, &task1->list_node);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(3);
}

/**
 * @test 在头节点前插入节点（尾插法）
 * @details 验证在链表尾部（头节点前）插入节点
 * @covers _mrtk_list_insert_before
 */
TEST_F(MrtkListTest, InsertBefore_AtHead_PrependToEnd) {
    /* Step 1: Given - 包含1个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);

    /* Step 2: When - 在头节点前插入task2（相当于追加到链表尾部） */
    _mrtk_list_insert_before(&test_list, &task2->list_node);

    /* Step 3: Then - 验证链表长度为2 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 2);

    /* Step 4: Then - 验证节点顺序：head -> task1 -> task2 -> head */
    EXPECT_EQ(test_list.next, &task1->list_node);
    EXPECT_EQ(task1->list_node.next, &task2->list_node);
    EXPECT_EQ(task2->list_node.next, &test_list);

    /* Step 5: Then - 验证task2是链表尾部 */
    EXPECT_EQ(test_list.prev, &task2->list_node);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(2);
}

/**
 * @test 在中间节点前插入
 * @details 验证在指定节点前插入新节点
 * @covers _mrtk_list_insert_before
 */
TEST_F(MrtkListTest, InsertBefore_InMiddle_BeforeSpecificNode) {
    /* Step 1: Given - 2个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task_before = create_task(99, 15);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task_before, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);

    /* Step 2: When - 在task2前插入task_before */
    _mrtk_list_insert_before(&task2->list_node, &task_before->list_node);

    /* Step 3: Then - 验证链表长度为3 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 3);

    /* Step 4: Then - 验证节点顺序：head -> task1 -> task_before -> task2 -> head */
    EXPECT_EQ(test_list.next, &task1->list_node);
    EXPECT_EQ(task1->list_node.next, &task_before->list_node);
    EXPECT_EQ(task_before->list_node.next, &task2->list_node);

    /* Step 5: Then - 验证链表完整性 */
    verify_list_integrity(3);
}

/* ==============================================================================
 * 删除测试
 * ============================================================================== */

/**
 * @test 删除链表中的唯一节点
 * @details 验证从单节点链表中删除节点后恢复为空链表
 * @covers _mrtk_list_remove
 */
TEST_F(MrtkListTest, Remove_SingleNode_ListBecomesEmpty) {
    /* Step 1: Given - 单节点链表 */
    test_task_t* task = create_task(1, 10);
    ASSERT_NE(task, nullptr);

    _mrtk_list_insert_after(&test_list, &task->list_node);
    EXPECT_EQ(_mrtk_list_len(&test_list), 1);

    /* Step 2: When - 删除唯一节点 */
    _mrtk_list_remove(&task->list_node);

    /* Step 3: Then - 验证链表恢复为空 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);

    /* Step 4: Then - 验证头节点指向自己 */
    EXPECT_EQ(test_list.next, &test_list);
    EXPECT_EQ(test_list.prev, &test_list);

    /* Step 5: Then - 验证被删除节点的指针指向自己（孤立项点） */
    EXPECT_EQ(task->list_node.next, &task->list_node);
    EXPECT_EQ(task->list_node.prev, &task->list_node);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(0);
}

/**
 * @test 删除头部节点
 * @details 验证删除链表第一个节点
 * @covers _mrtk_list_remove
 */
TEST_F(MrtkListTest, Remove_HeadNode_RestOfListIntact) {
    /* Step 1: Given - 3个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 2: When - 删除头部节点task1 */
    _mrtk_list_remove(&task1->list_node);

    /* Step 3: Then - 验证链表长度为2 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 2);

    /* Step 4: Then - 验证新的头部是task2 */
    EXPECT_EQ(test_list.next, &task2->list_node);
    EXPECT_EQ(task2->list_node.prev, &test_list);

    /* Step 5: Then - 验证剩余节点顺序：head -> task2 -> task3 -> head */
    EXPECT_EQ(task2->list_node.next, &task3->list_node);
    EXPECT_EQ(task3->list_node.next, &test_list);

    /* Step 6: Then - 验证被删除节点task1成为孤立项点 */
    EXPECT_EQ(task1->list_node.next, &task1->list_node);
    EXPECT_EQ(task1->list_node.prev, &task1->list_node);

    /* Step 7: Then - 验证链表完整性 */
    verify_list_integrity(2);
}

/**
 * @test 删除尾部节点
 * @details 验证删除链表最后一个节点
 * @covers _mrtk_list_remove
 */
TEST_F(MrtkListTest, Remove_TailNode_HeadPointsToNewTail) {
    /* Step 1: Given - 3个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 2: When - 删除尾部节点task3 */
    _mrtk_list_remove(&task3->list_node);

    /* Step 3: Then - 验证链表长度为2 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 2);

    /* Step 4: Then - 验证新的尾部是task2 */
    EXPECT_EQ(test_list.prev, &task2->list_node);
    EXPECT_EQ(task2->list_node.next, &test_list);

    /* Step 5: Then - 验证被删除节点task3成为孤立项点 */
    EXPECT_EQ(task3->list_node.next, &task3->list_node);
    EXPECT_EQ(task3->list_node.prev, &task3->list_node);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(2);
}

/**
 * @test 删除中间节点
 * @details 验证删除链表中间节点后前后节点正确连接
 * @covers _mrtk_list_remove
 */
TEST_F(MrtkListTest, Remove_MiddleNode_NeighborNodesConnected) {
    /* Step 1: Given - 3个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 2: When - 删除中间节点task2 */
    _mrtk_list_remove(&task2->list_node);

    /* Step 3: Then - 验证链表长度为2 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 2);

    /* Step 4: Then - 验证task1和task3直接连接 */
    EXPECT_EQ(task1->list_node.next, &task3->list_node);
    EXPECT_EQ(task3->list_node.prev, &task1->list_node);

    /* Step 5: Then - 验证被删除节点task2成为孤立项点 */
    EXPECT_EQ(task2->list_node.next, &task2->list_node);
    EXPECT_EQ(task2->list_node.prev, &task2->list_node);

    /* Step 6: Then - 验证链表完整性 */
    verify_list_integrity(2);
}

/* ==============================================================================
 * 长度与判空测试
 * ============================================================================== */

/**
 * @test 获取空链表长度
 * @details 验证空链表的长度为0（边界值）
 * @covers _mrtk_list_len
 */
TEST_F(MrtkListTest, Len_EmptyList_ReturnsZero) {
    /* Step 1: Given - 空链表 */

    /* Step 2: When - 获取链表长度 */
    mrtk_u32_t len = _mrtk_list_len(&test_list);

    /* Step 3: Then - 验证长度为0 */
    EXPECT_EQ(len, 0);
}

/**
 * @test 获取单节点链表长度
 * @details 验证单节点链表的长度为1（边界值）
 * @covers _mrtk_list_len
 */
TEST_F(MrtkListTest, Len_SingleNode_ReturnsOne) {
    /* Step 1: Given - 单节点链表 */
    test_task_t* task = create_task(1, 10);
    ASSERT_NE(task, nullptr);

    _mrtk_list_insert_after(&test_list, &task->list_node);

    /* Step 2: When - 获取链表长度 */
    mrtk_u32_t len = _mrtk_list_len(&test_list);

    /* Step 3: Then - 验证长度为1 */
    EXPECT_EQ(len, 1);
}

/**
 * @test 获取多节点链表长度
 * @details 验证多节点链表的长度正确
 * @covers _mrtk_list_len
 */
TEST_F(MrtkListTest, Len_MultipleNodes_ReturnsCorrectCount) {
    /* Step 1: Given - 5个节点的链表 */
    for (int i = 0; i < 5; i++) {
        test_task_t* task = create_task(i + 1, i * 10);
        ASSERT_NE(task, nullptr);

        if (i == 0) {
            _mrtk_list_insert_after(&test_list, &task->list_node);
        } else {
            _mrtk_list_insert_after(&tasks[i - 1].list_node, &task->list_node);
        }
    }

    /* Step 2: When - 获取链表长度 */
    mrtk_u32_t len = _mrtk_list_len(&test_list);

    /* Step 3: Then - 验证长度为5 */
    EXPECT_EQ(len, 5);
}

/**
 * @test 判空函数-空链表
 * @details 验证空链表判空返回TRUE（分支覆盖：head->next == head）
 * @covers _mrtk_list_is_empty
 */
TEST_F(MrtkListTest, IsEmpty_EmptyList_ReturnsTRUE) {
    /* Step 1: Given - 空链表 */

    /* Step 2: When - 判断链表是否为空 */
    mrtk_bool_t is_empty = _mrtk_list_is_empty(&test_list);

    /* Step 3: Then - 验证返回TRUE */
    EXPECT_EQ(is_empty, MRTK_TRUE);
}

/**
 * @test 判空函数-非空链表
 * @details 验证非空链表判空返回FALSE（分支覆盖：head->next != head）
 * @covers _mrtk_list_is_empty
 */
TEST_F(MrtkListTest, IsEmpty_NonEmptyList_ReturnsFALSE) {
    /* Step 1: Given - 包含1个节点的链表 */
    test_task_t* task = create_task(1, 10);
    ASSERT_NE(task, nullptr);

    _mrtk_list_insert_after(&test_list, &task->list_node);

    /* Step 2: When - 判断链表是否为空 */
    mrtk_bool_t is_empty = _mrtk_list_is_empty(&test_list);

    /* Step 3: Then - 验证返回FALSE */
    EXPECT_EQ(is_empty, MRTK_FALSE);
}

/* ==============================================================================
 * 遍历宏测试
 * ============================================================================== */

/**
 * @test MRTK_LIST_FOR_EACH遍历空链表
 * @details 验证遍历宏在空链表上不进入循环体
 * @covers MRTK_LIST_FOR_EACH
 */
TEST_F(MrtkListTest, ForEach_EmptyList_NoIteration) {
    /* Step 1: Given - 空链表 */
    mrtk_u32_t count = 0;

    /* Step 2: When - 遍历空链表 */
    test_task_t* task;
    MRTK_LIST_FOR_EACH(task, &test_list, test_task_t, list_node) {
        count++;
    }

    /* Step 3: Then - 验证循环体未执行 */
    EXPECT_EQ(count, 0);
}

/**
 * @test MRTK_LIST_FOR_EACH遍历单节点链表
 * @details 验证遍历宏能正确访问单个节点
 * @covers MRTK_LIST_FOR_EACH
 */
TEST_F(MrtkListTest, ForEach_SingleNode_VisitsOnce) {
    /* Step 1: Given - 单节点链表 */
    test_task_t* task = create_task(1, 10);
    ASSERT_NE(task, nullptr);

    _mrtk_list_insert_after(&test_list, &task->list_node);

    /* Step 2: When - 遍历链表并累加task_id */
    mrtk_u32_t sum       = 0;
    mrtk_u32_t iteration = 0;
    test_task_t* current;

    MRTK_LIST_FOR_EACH(current, &test_list, test_task_t, list_node) {
        sum += current->task_id;
        iteration++;
    }

    /* Step 3: Then - 验证只遍历了1次 */
    EXPECT_EQ(iteration, 1);

    /* Step 4: Then - 验证task_id正确累加 */
    EXPECT_EQ(sum, 1);
}

/**
 * @test MRTK_LIST_FOR_EACH遍历多节点链表
 * @details 验证遍历宏能按顺序访问所有节点
 * @covers MRTK_LIST_FOR_EACH
 */
TEST_F(MrtkListTest, ForEach_MultipleNodes_VisitsAllInOrder) {
    /* Step 1: Given - 5个节点的链表，task_id依次为1,2,3,4,5 */
    mrtk_u32_t expected_ids[] = {1, 2, 3, 4, 5};

    for (int i = 0; i < 5; i++) {
        test_task_t* task = create_task(expected_ids[i], i * 10);
        ASSERT_NE(task, nullptr);

        if (i == 0) {
            _mrtk_list_insert_after(&test_list, &task->list_node);
        } else {
            _mrtk_list_insert_after(&tasks[i - 1].list_node, &task->list_node);
        }
    }

    /* Step 2: When - 遍历链表并收集task_id */
    mrtk_u32_t collected_ids[5] = {0};
    mrtk_u32_t index           = 0;
    test_task_t* current;

    MRTK_LIST_FOR_EACH(current, &test_list, test_task_t, list_node) {
        ASSERT_LT(index, 5);  /* 防止数组越界 */
        collected_ids[index++] = current->task_id;
    }

    /* Step 3: Then - 验证遍历了5次 */
    EXPECT_EQ(index, 5);

    /* Step 4: Then - 验证顺序正确 */
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(collected_ids[i], expected_ids[i]);
    }
}

/**
 * @test MRTK_LIST_FOR_EACH_SAFE遍历并删除所有节点
 * @details 验证安全遍历宏可以在遍历过程中删除节点
 * @covers MRTK_LIST_FOR_EACH_SAFE
 */
TEST_F(MrtkListTest, ForEachSafe_RemoveAllNodes_ListBecomesEmpty) {
    /* Step 1: Given - 3个节点的链表 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 2: When - 使用SAFE宏遍历并删除所有节点 */
    test_task_t* current;
    test_task_t* next;

    MRTK_LIST_FOR_EACH_SAFE(current, next, &test_list, test_task_t, list_node) {
        _mrtk_list_remove(&current->list_node);
    }

    /* Step 3: Then - 验证链表为空 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);

    /* Step 4: Then - 验证所有节点成为孤立项点 */
    EXPECT_EQ(task1->list_node.next, &task1->list_node);
    EXPECT_EQ(task2->list_node.next, &task2->list_node);
    EXPECT_EQ(task3->list_node.next, &task3->list_node);

    /* Step 5: Then - 验证链表完整性 */
    verify_list_integrity(0);
}

/**
 * @test MRTK_LIST_FOR_EACH_SAFE间隔删除节点
 * @details 验证安全遍历宏可以在遍历过程中有选择地删除节点
 * @covers MRTK_LIST_FOR_EACH_SAFE
 */
TEST_F(MrtkListTest, ForEachSafe_RemoveEverySecondNode) {
    /* Step 1: Given - 5个节点的链表 */
    mrtk_u32_t expected_remaining[] = {1, 3, 5};  /* 期望保留的节点 */

    for (int i = 0; i < 5; i++) {
        test_task_t* task = create_task(i + 1, i * 10);
        ASSERT_NE(task, nullptr);

        if (i == 0) {
            _mrtk_list_insert_after(&test_list, &task->list_node);
        } else {
            _mrtk_list_insert_after(&tasks[i - 1].list_node, &task->list_node);
        }
    }

    /* Step 2: When - 遍历并删除task_id为偶数的节点（2和4） */
    test_task_t* current;
    test_task_t* next;
    mrtk_u32_t   iteration = 0;

    MRTK_LIST_FOR_EACH_SAFE(current, next, &test_list, test_task_t, list_node) {
        if (current->task_id % 2 == 0) {
            /* 删除偶数ID节点 */
            _mrtk_list_remove(&current->list_node);
        }
        iteration++;
    }

    /* Step 3: Then - 验证遍历了5次（即使删除了节点） */
    EXPECT_EQ(iteration, 5);

    /* Step 4: Then - 验证链表剩余3个节点 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 3);

    /* Step 5: Then - 验证剩余节点为1, 3, 5 */
    mrtk_u32_t index = 0;
    test_task_t* remaining;

    MRTK_LIST_FOR_EACH(remaining, &test_list, test_task_t, list_node) {
        EXPECT_EQ(remaining->task_id, expected_remaining[index++]);
    }

    EXPECT_EQ(index, 3);
}

/* ==============================================================================
 * 容器宏测试（_mrtk_list_entry / MRTK_CONTAINER_OF）
 * ============================================================================== */

/**
 * @test _mrtk_list_entry获取宿主结构体指针
 * @details 验证通过链表节点正确获取包含该节点的结构体指针
 * @covers _mrtk_list_entry, MRTK_CONTAINER_OF
 */
TEST_F(MrtkListTest, ListEntry_ValidNode_ReturnsCorrectStructurePointer) {
    /* Step 1: Given - 已知的结构体指针 */
    test_task_t* original_task = create_task(42, 99);
    ASSERT_NE(original_task, nullptr);

    /* Step 2: When - 通过list_node获取结构体指针 */
    test_task_t* derived_task = (test_task_t*)_mrtk_list_entry(&original_task->list_node,
                                                                 test_task_t,
                                                                 list_node);

    /* Step 3: Then - 验证派生指针与原始指针相同 */
    EXPECT_EQ(derived_task, original_task);

    /* Step 4: Then - 验证可以通过派生指针访问成员 */
    EXPECT_EQ(derived_task->task_id, 42);
    EXPECT_EQ(derived_task->priority, 99);
}

/**
 * @test _mrtk_list_entry在遍历中使用
 * @details 验证在实际遍历场景中使用容器宏的正确性
 * @covers _mrtk_list_entry, MRTK_LIST_FOR_EACH
 */
TEST_F(MrtkListTest, ListEntry_InTraversal_AllPointersValid) {
    /* Step 1: Given - 3个节点的链表 */
    test_task_t* tasks_to_add[3];
    mrtk_u32_t   task_ids[] = {10, 20, 30};

    for (int i = 0; i < 3; i++) {
        tasks_to_add[i] = create_task(task_ids[i], i * 10);
        ASSERT_NE(tasks_to_add[i], nullptr);

        if (i == 0) {
            _mrtk_list_insert_after(&test_list, &tasks_to_add[i]->list_node);
        } else {
            _mrtk_list_insert_after(&tasks_to_add[i - 1]->list_node, &tasks_to_add[i]->list_node);
        }
    }

    /* Step 2: When - 遍历并使用list_entry获取指针 */
    mrtk_list_node_t* node;
    mrtk_u32_t        index = 0;

    for (node = test_list.next; node != &test_list; node = node->next) {
        /* 使用list_entry获取包含结构体指针 */
        test_task_t* task = (test_task_t*)_mrtk_list_entry(node, test_task_t, list_node);

        /* Step 3: Then - 验证获取的指针正确 */
        EXPECT_EQ(task, tasks_to_add[index]);
        EXPECT_EQ(task->task_id, task_ids[index]);

        index++;
    }

    /* Step 4: Then - 验证遍历了3个节点 */
    EXPECT_EQ(index, 3);
}

/* ==============================================================================
 * 状态机与生命周期测试
 * ============================================================================== */

/**
 * @test 完整生命周期测试
 * @details 验证链表从初始化到添加、删除、清空的完整状态转换
 * @covers 完整的状态机覆盖
 */
TEST_F(MrtkListTest, CompleteLifecycle_Init_Add_Remove_Clear) {
    /* ========== 状态1: Init ========== */
    /* Step 1: Given - 初始化后的空链表 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);
    verify_list_integrity(0);

    /* ========== 状态2: Add (添加3个节点) ========== */
    /* Step 2: When - 添加3个节点 */
    test_task_t* task1 = create_task(1, 10);
    test_task_t* task2 = create_task(2, 20);
    test_task_t* task3 = create_task(3, 30);
    ASSERT_NE(task1, nullptr);
    ASSERT_NE(task2, nullptr);
    ASSERT_NE(task3, nullptr);

    _mrtk_list_insert_after(&test_list, &task1->list_node);
    _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
    _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

    /* Step 3: Then - 验证状态转换到有3个节点 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_FALSE);
    EXPECT_EQ(_mrtk_list_len(&test_list), 3);
    verify_list_integrity(3);

    /* ========== 状态3: Remove (删除1个节点) ========== */
    /* Step 4: When - 删除中间节点 */
    _mrtk_list_remove(&task2->list_node);

    /* Step 5: Then - 验证状态转换到有2个节点 */
    EXPECT_EQ(_mrtk_list_len(&test_list), 2);
    verify_list_integrity(2);

    /* ========== 状态4: Clear (清空链表) ========== */
    /* Step 6: When - 删除所有剩余节点 */
    _mrtk_list_remove(&task1->list_node);
    _mrtk_list_remove(&task3->list_node);

    /* Step 7: Then - 验证状态恢复到初始空链表 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);
    verify_list_integrity(0);

    /* Step 8: Then - 验证所有节点成为孤立项点 */
    EXPECT_EQ(task1->list_node.next, &task1->list_node);
    EXPECT_EQ(task2->list_node.next, &task2->list_node);
    EXPECT_EQ(task3->list_node.next, &task3->list_node);
}

/* ==============================================================================
 * 边界值与压力测试
 * ============================================================================== */

/**
 * @test 大量节点边界测试
 * @details 验证链表能正确处理大量节点（1000个节点）
 * @covers 边界值分析：大量节点
 */
TEST_F(MrtkListTest, BoundaryTest_LargeNumberOfNodes) {
    /* Step 1: Given - 准备插入1000个节点 */
    const mrtk_u32_t NODE_COUNT = 1000;
    test_task_t*        prev_task = nullptr;

    /* 注意：由于tasks数组只有10个元素，这里使用动态分配 */
    test_task_t* large_tasks = new test_task_t[NODE_COUNT];

    /* Step 2: When - 依次插入1000个节点 */
    for (mrtk_u32_t i = 0; i < NODE_COUNT; i++) {
        large_tasks[i].task_id  = i + 1;
        large_tasks[i].priority = (mrtk_u8_t)(i % 256);
        _mrtk_list_init(&large_tasks[i].list_node);

        if (prev_task == nullptr) {
            _mrtk_list_insert_after(&test_list, &large_tasks[i].list_node);
        } else {
            _mrtk_list_insert_after(&prev_task->list_node, &large_tasks[i].list_node);
        }

        prev_task = &large_tasks[i];
    }

    /* Step 3: Then - 验证链表长度为1000 */
    EXPECT_EQ(_mrtk_list_len(&test_list), NODE_COUNT);

    /* Step 4: Then - 验证链表完整性 */
    verify_list_integrity(NODE_COUNT);

    /* Step 5: Then - 验证第一个和最后一个节点 */
    EXPECT_EQ(test_list.next, &large_tasks[0].list_node);
    EXPECT_EQ(test_list.prev, &large_tasks[NODE_COUNT - 1].list_node);

    /* Step 6: Then - 验证中间节点的连接 */
    EXPECT_EQ(large_tasks[0].list_node.next, &large_tasks[1].list_node);
    EXPECT_EQ(large_tasks[NODE_COUNT - 2].list_node.next, &large_tasks[NODE_COUNT - 1].list_node);
    EXPECT_EQ(large_tasks[NODE_COUNT - 1].list_node.next, &test_list);

    /* Step 7: Then - 验证反向指针 */
    EXPECT_EQ(large_tasks[1].list_node.prev, &large_tasks[0].list_node);
    EXPECT_EQ(large_tasks[NODE_COUNT - 1].list_node.prev, &large_tasks[NODE_COUNT - 2].list_node);
    EXPECT_EQ(large_tasks[0].list_node.prev, &test_list);

    /* 清理 */
    delete[] large_tasks;
}

/**
 * @test 频繁插入删除混合操作
 * @details 验证链表在频繁插入删除操作下的稳定性
 * @covers 压力测试：混合操作
 */
TEST_F(MrtkListTest, StressTest_MixedInsertAndRemoveOperations) {
    /* Step 1: Given - 空链表 */
    const int ITERATIONS = 100;

    /* Step 2: When - 执行100次插入删除循环 */
    for (int i = 0; i < ITERATIONS; i++) {
        /* 插入3个节点 */
        test_task_t* task1 = create_task(i * 3 + 1, 10);
        test_task_t* task2 = create_task(i * 3 + 2, 20);
        test_task_t* task3 = create_task(i * 3 + 3, 30);

        if (task1 == nullptr || task2 == nullptr || task3 == nullptr) {
            /* tasks数组用完了，重置计数器 */
            task_counter = 0;
            task1 = create_task(i * 3 + 1, 10);
            task2 = create_task(i * 3 + 2, 20);
            task3 = create_task(i * 3 + 3, 30);
        }

        _mrtk_list_insert_after(&test_list, &task1->list_node);
        _mrtk_list_insert_after(&task1->list_node, &task2->list_node);
        _mrtk_list_insert_after(&task2->list_node, &task3->list_node);

        /* 验证插入后状态 */
        EXPECT_EQ(_mrtk_list_len(&test_list), 3);
        verify_list_integrity(3);

        /* 删除中间节点 */
        _mrtk_list_remove(&task2->list_node);

        /* 验证删除后状态 */
        EXPECT_EQ(_mrtk_list_len(&test_list), 2);
        verify_list_integrity(2);

        /* 删除剩余节点 */
        _mrtk_list_remove(&task1->list_node);
        _mrtk_list_remove(&task3->list_node);

        /* 验证清空后状态 */
        EXPECT_EQ(_mrtk_list_len(&test_list), 0);
        verify_list_integrity(0);
    }

    /* Step 3: Then - 验证最终状态为空链表 */
    EXPECT_EQ(_mrtk_list_is_empty(&test_list), MRTK_TRUE);
    EXPECT_EQ(_mrtk_list_len(&test_list), 0);
}

/**
 * @test 尾部连续插入压力测试
 * @details 验证在链表尾部连续插入多个节点（使用insert_before实现）
 * @covers 压力测试：尾部插入
 * @note insert_before(&head, &node) 实际上在循环链表的尾部追加节点
 */
TEST_F(MrtkListTest, StressTest_PrependAtHeadMultipleTimes) {
    /* Step 1: Given - 空链表 */
    const int INSERT_COUNT = 50;

    /* 注意：由于tasks数组只有10个元素，这里使用动态分配 */
    test_task_t* prepend_tasks = new test_task_t[INSERT_COUNT];

    /* 关键：先清零整个数组，避免未初始化内存污染 */
    memset(prepend_tasks, 0, sizeof(test_task_t) * INSERT_COUNT);

    /* Step 2: When - 在头部连续插入50个节点（使用insert_before） */
    for (int i = 0; i < INSERT_COUNT; i++) {
        /* 初始化任务 */
        prepend_tasks[i].task_id  = i + 1;
        prepend_tasks[i].priority = (mrtk_u8_t)(i * 10);
        _mrtk_list_init(&prepend_tasks[i].list_node);

        /* 在头节点前插入（相当于prepend） */
        _mrtk_list_insert_before(&test_list, &prepend_tasks[i].list_node);
    }

    /* Step 3: Then - 验证链表长度 */
    EXPECT_EQ(_mrtk_list_len(&test_list), INSERT_COUNT);

    /* Step 4: Then - 验证链表完整性 */
    verify_list_integrity(INSERT_COUNT);

    /* Step 5: Then - 验证插入顺序 */
    /* 注意：insert_before(&head, &node) 实际上是在尾部追加，所以顺序是正序的 */
    test_task_t* current;
    mrtk_u32_t   expected_id = 1;  /* 第一个插入的ID最小 */
    mrtk_u32_t   count       = 0;

    MRTK_LIST_FOR_EACH(current, &test_list, test_task_t, list_node) {
        EXPECT_EQ(current->task_id, expected_id + count);
        count++;
    }

    EXPECT_EQ(count, INSERT_COUNT);

    /* 清理 */
    delete[] prepend_tasks;
}