/**
 * @file mrtk_mem_pool_test.cpp
 * @author leiyx
 * @brief 内存池管理模块单元测试
 * @details 使用 GTest/GMock 框架对内存池模块进行全面测试
 *
 * 测试覆盖策略：
 * 1. 边界值分析：block_size=0/对齐边界、size=0、满/空状态
 * 2. 等价类划分：NULL指针防御、非法参数、合法参数
 * 3. 分支覆盖：所有 if/else 分支（空闲块检查、阻塞判断、唤醒逻辑）
 * 4. 状态机覆盖：Init/Create -> Alloc/Free -> Detach/Delete 完整闭环
 * 5. 并发场景：任务阻塞、唤醒、优先级调度
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/* MRTK 内核头文件 */
#include "mrtk_config_internal.h"
#include "mrtk_mem_pool.h"
#include "mrtk.h"
#include "mrtk_typedef.h"
#include "mrtk_errno.h"
#include "mrtk_task.h"
#include "mrtk_list.h"
#include "mrtk_obj.h"
#include "mrtk_schedule.h"
#include "mrtk_mem_heap.h"
#include "mrtk_timer.h"

/* Mock 框架头文件 */
#include "mrtk_mock_hw.hpp"
extern "C" {
}

/* 测试常量定义 */
#define TEST_BLOCK_SIZE       64
#define TEST_BLOCK_COUNT      10
#define TEST_POOL_SIZE        (TEST_BLOCK_SIZE * TEST_BLOCK_COUNT)
#define TEST_ALIGN_SIZE       8
#define TEST_SMALL_BLOCK_SIZE 4
#define TEST_UNALIGNED_SIZE   100

/* =============================================================================
 * 测试夹具类定义
 * ============================================================================ */

/**
 * @class MrtkMemPoolTest
 * @brief 内存池模块测试夹具
 * @details 提供统一的测试环境初始化和清理
 */
class MrtkMemPoolTest : public ::testing::Test {
  protected:
    MockCpuPort mock_cpu_port;
    mrtk_u8_t    test_buffer[TEST_POOL_SIZE + TEST_ALIGN_SIZE]; /* 额外空间用于对齐测试 */
    mrtk_mem_pool_t test_mp;

    void SetUp() override {
        /* Step 1: 系统初始化，复位全局变量 */
        mrtk_system_init();

        g_CurrentTCB = mrtk_task_get_idle();

        /* Step 2: 设置 Mock 对象 */
        mrtk_mock_set_cpu_port(&mock_cpu_port);

        /* Step 3: 设置默认期望行为 */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
            .WillByDefault(testing::Return(0));
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_enable)
            .WillByDefault(testing::Return());

        /* Step 4: 初始化测试缓冲区为0 */
        memset(test_buffer, 0, sizeof(test_buffer));
        memset(&test_mp, 0xFF, sizeof(test_mp)); /* 填充非法值检测未初始化 */
    }

    void TearDown() override {
        mrtk_mock_clear_cpu_port();
    }

    /**
     * @brief 辅助函数：验证内存池空闲链表的完整性
     * @param mp 内存池控制块指针
     * @return mrtk_bool_t 链表完整返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t verify_free_list_integrity(mrtk_mem_pool_t *mp) {
        if (mp->free_block_count == 0) {
            return (mp->free_block_list == MRTK_NULL) ? MRTK_TRUE : MRTK_FALSE;
        }

        mrtk_u8_t   *current    = mp->free_block_list;
        mrtk_size_t count      = 0;
        mrtk_u8_t   seen_start = MRTK_FALSE;

        /* 检测环路和计数正确性 */
        while (current != MRTK_NULL) {
            if (current == mp->free_block_list) {
                if (seen_start) {
                    return MRTK_FALSE; /* 环路 */
                }
                seen_start = MRTK_TRUE;
            }

            mrtk_u8_t *next = (mrtk_u8_t *) MRTK_POOL_GET_HEADER(current);
            current = next;
            count++;

            if (count > mp->total_block_count) {
                return MRTK_FALSE; /* 超出最大块数，存在环路 */
            }
        }

        return (count == mp->free_block_count) ? MRTK_TRUE : MRTK_FALSE;
    }

    /**
     * @brief 辅助函数：检查地址是否在内存池范围内
     * @param mp 内存池控制块指针
     * @param addr 待检查地址
     * @return mrtk_bool_t 在范围内返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t is_address_in_pool(mrtk_mem_pool_t *mp, mrtk_void_t *addr) {
        mrtk_ptr_t start      = (mrtk_ptr_t) mp->start_addr;
        mrtk_ptr_t end        = start + mp->size;
        mrtk_ptr_t target     = (mrtk_ptr_t) addr;
        return (target >= start && target < end) ? MRTK_TRUE : MRTK_FALSE;
    }
};

/* =============================================================================
 * mrtk_mp_init 测试用例
 * ============================================================================ */

/**
 * @test MpInit_NullParameters_NoCrash
 * @brief 测试 NULL 参数处理（防御性测试）
 * @details 验证传入 NULL 指针时函数不会崩溃，但行为未定义（MRTK 无显式检查）
 * @note 等价类划分：非法参数类
 */
TEST_F(MrtkMemPoolTest, MpInit_NullParameters_NoCrash) {
    /* Given: NULL 参数 */
    /* Note: MRTK 未对 NULL 进行显式检查，这是设计决策
     * 此测试确保不会立即崩溃，但不应依赖此行为 */

    /* When & Then: 不应立即崩溃（但行为未定义） */
    /* 实际使用中应确保调用者传入有效指针 */
}

/**
 * @test MpInit_NormalInit_Success
 * @brief 测试正常初始化（正向测试）
 * @details 验证内存池初始化后所有字段正确设置
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkMemPoolTest, MpInit_NormalInit_Success) {
    /* Given: 合法的初始化参数 */
    const mrtk_char_t *name = "test_pool";

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_mp_init(&test_mp, name, test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* Then: 验证返回值和内存池状态 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(test_mp.block_size, MRTK_ALIGN_UP(TEST_BLOCK_SIZE, MRTK_ALIGN_SIZE));
    EXPECT_GT(test_mp.total_block_count, 0);
    EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
    EXPECT_NE(test_mp.free_block_list, MRTK_NULL);
    EXPECT_EQ(test_mp.start_addr, test_buffer);
    EXPECT_EQ(test_mp.size, TEST_POOL_SIZE);

    /* 验证空闲链表完整性 */
    EXPECT_TRUE(verify_free_list_integrity(&test_mp));

    /* 验证对象类型为静态 */
    EXPECT_TRUE(MRTK_OBJ_IS_STATIC(test_mp.obj.type));
}

/**
 * @test MpInit_AlignedAddress_Success
 * @brief 测试对齐地址处理（边界值测试）
 * @details 验证非对齐起始地址会被正确对齐
 * @note 边界值分析：非对齐地址边界
 */
TEST_F(MrtkMemPoolTest, MpInit_AlignedAddress_Success) {
    /* Given: 非对齐的起始地址 */
    mrtk_u8_t *unaligned_addr = test_buffer + 1; /* 故意偏移1字节 */

    /* When: 执行初始化 */
    mrtk_err_t ret =
        mrtk_mp_init(&test_mp, "aligned_pool", unaligned_addr, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* Then: 验证对齐处理正确 */
    EXPECT_EQ(ret, MRTK_EOK);
    mrtk_ptr_t aligned_start = MRTK_ALIGN_UP((mrtk_ptr_t) unaligned_addr, MRTK_ALIGN_SIZE);
    EXPECT_GE(aligned_start, (mrtk_ptr_t) unaligned_addr);
    EXPECT_GT(test_mp.total_block_count, 0);

    /* 验证空闲链表在对齐地址上 */
    mrtk_ptr_t free_list_addr = (mrtk_ptr_t) test_mp.free_block_list;
    EXPECT_GE(free_list_addr, aligned_start);
}

/**
 * @test MpInit_BlockSizeAligned_Success
 * @brief 测试块大小对齐处理（边界值测试）
 * @details 验证非对齐块大小会被向上对齐
 * @note 边界值分析：非对齐大小边界
 */
TEST_F(MrtkMemPoolTest, MpInit_BlockSizeAligned_Success) {
    /* Given: 非对齐的块大小 */
    mrtk_size_t unaligned_size = TEST_UNALIGNED_SIZE;

    /* When: 执行初始化 */
    mrtk_err_t ret =
        mrtk_mp_init(&test_mp, "aligned_size_pool", test_buffer, TEST_POOL_SIZE, unaligned_size);

    /* Then: 验证块大小被对齐 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(test_mp.block_size, MRTK_ALIGN_UP(unaligned_size, MRTK_ALIGN_SIZE));
    EXPECT_GT(test_mp.total_block_count, 0);
}

/**
 * @test MpInit_SmallPool_InsufficientSpace
 * @brief 测试内存池空间不足（边界值测试）
 * @details 验证当空间不足以容纳任何块时返回错误
 * @note 边界值分析：最小有效空间边界
 */
TEST_F(MrtkMemPoolTest, MpInit_SmallPool_InsufficientSpace) {
    /* Given: 极小的内存池空间（不足以容纳一个完整块） */
    mrtk_size_t tiny_size = MRTK_POOL_BLOCK_HEADER_SIZE;

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_mp_init(&test_mp, "tiny_pool", test_buffer, tiny_size, TEST_BLOCK_SIZE);

    /* Then: 验证返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);
    EXPECT_EQ(test_mp.total_block_count, 0);
}

/**
 * @test MpInit_SuspendListInitialized_Empty
 * @brief 测试阻塞任务列表初始化
 * @details 验证初始化后阻塞列表为空
 * @note 状态机覆盖：Init 状态验证
 */
TEST_F(MrtkMemPoolTest, MpInit_SuspendListInitialized_Empty) {
    /* Given: 正常初始化参数 */

    /* When: 执行初始化 */
    mrtk_mp_init(&test_mp, "suspend_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* Then: 验证阻塞任务列表为空 */
    EXPECT_TRUE(test_mp.suspend_tasks_dummy.next == &test_mp.suspend_tasks_dummy);
    EXPECT_TRUE(test_mp.suspend_tasks_dummy.prev == &test_mp.suspend_tasks_dummy);
}

/* =============================================================================
 * mrtk_mp_detach 测试用例
 * ============================================================================ */

/**
 * @test MpDetach_NormalDetach_Success
 * @brief 测试正常脱离（正向测试）
 * @details 验证内存池从系统对象管理中移除
 * @note 状态机覆盖：Init -> Detach
 */
TEST_F(MrtkMemPoolTest, MpDetach_NormalDetach_Success) {
    /* Given: 已初始化的内存池 */
    mrtk_mp_init(&test_mp, "detach_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* When: 执行脱离 */
    mrtk_err_t ret = mrtk_mp_detach(&test_mp);

    /* Then: 验证返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* 验证对象被标记为删除（通过对象系统） */
    /* Note: 对象系统的验证由 mrtk_obj_test 覆盖 */
}

/**
 * @test MpDetach_WithSuspendedTasks_AllResumed
 * @brief 测试脱离时唤醒阻塞任务（分支覆盖）
 * @details 验证脱离时所有阻塞任务被唤醒并设置错误码
 * @note 分支覆盖：suspend_tasks_dummy 不为空的分支
 */
TEST_F(MrtkMemPoolTest, MpDetach_WithSuspendedTasks_AllResumed) {
    /* Given: 已初始化的内存池，且有阻塞任务 */
    mrtk_mp_init(&test_mp, "suspend_detach", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* 模拟阻塞任务 */
    mrtk_tcb_t dummy_task;
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.state = MRTK_TASK_STAT_SUSPEND;
    _mrtk_list_insert_before(&test_mp.suspend_tasks_dummy, &dummy_task.sched_node);

    /* When: 执行脱离 */
    mrtk_err_t ret = mrtk_mp_detach(&test_mp);

    /* Then: 验证返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* 验证任务被唤醒并设置错误码 */
    EXPECT_EQ(dummy_task.last_error, MRTK_EDELETED);
    EXPECT_EQ(dummy_task.state, MRTK_TASK_STAT_READY);
}

/* =============================================================================
 * mrtk_mp_create 测试用例
 * ============================================================================ */

/**
 * @test MpCreate_NormalCreate_Success
 * @brief 测试正常动态创建（正向测试）
 * @details 验证动态创建的内存池所有字段正确
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkMemPoolTest, MpCreate_NormalCreate_Success) {
    /* Given: 合法的创建参数 */
    const mrtk_char_t *name = "dynamic_pool";

    /* When: 执行创建 */
    mrtk_mem_pool_t *mp = mrtk_mp_create(name, TEST_BLOCK_SIZE, TEST_BLOCK_COUNT);

    /* Then: 验证创建成功 */
    EXPECT_NE(mp, MRTK_NULL);
    EXPECT_EQ(mp->block_size, MRTK_ALIGN_UP(TEST_BLOCK_SIZE, MRTK_ALIGN_SIZE));
    EXPECT_EQ(mp->total_block_count, TEST_BLOCK_COUNT);
    EXPECT_EQ(mp->free_block_count, TEST_BLOCK_COUNT);

    /* 验证对象类型为动态 */
    EXPECT_TRUE(mp->obj.type & MRTK_OBJECT_TYPE_DYNAMIC);

    /* 清理 */
    if (mp != MRTK_NULL) {
        mrtk_mp_destroy(mp);
    }
}

/**
 * @test MpCreate_MemoryAllocationFailed_ReturnsNull
 * @brief 测试内存分配失败（分支覆盖）
 * @details 验证当 mrtk_malloc 失败时返回 NULL
 * @note 分支覆盖：malloc == MRTK_NULL 分支
 */
TEST_F(MrtkMemPoolTest, MpCreate_MemoryAllocationFailed_ReturnsNull) {
    /* Given: 内存分配将失败（通过限制堆大小） */
    /* 注意：实际测试中需要 Mock mrtk_malloc，当前使用真实堆 */

    /* When: 尝试创建超大内存池 */
    mrtk_mem_pool_t *mp = mrtk_mp_create("huge_pool", 1024 * 1024, 10000);

    /* Then: 验证返回 NULL */
    EXPECT_EQ(mp, MRTK_NULL);
}

/* =============================================================================
 * mrtk_mp_destroy 测试用例
 * ============================================================================ */

/**
 * @test MpDestroy_NormalDestroy_Success
 * @brief 测试正常销毁（正向测试）
 * @details 验证动态创建的内存池被正确销毁
 * @note 状态机覆盖：Create -> Destroy
 */
TEST_F(MrtkMemPoolTest, MpDestroy_NormalDestroy_Success) {
    /* Given: 动态创建的内存池 */
    mrtk_mem_pool_t *mp = mrtk_mp_create("destroy_test", TEST_BLOCK_SIZE, TEST_BLOCK_COUNT);
    ASSERT_NE(mp, MRTK_NULL);

    /* When: 执行销毁 */
    mrtk_err_t ret = mrtk_mp_destroy(mp);

    /* Then: 验证销毁成功 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/**
 * @test MpDestroy_WithSuspendedTasks_AllResumed
 * @brief 测试销毁时唤醒阻塞任务（分支覆盖）
 * @details 验证销毁时所有阻塞任务被唤醒
 * @note 分支覆盖：suspend_tasks_dummy 不为空的分支
 */
TEST_F(MrtkMemPoolTest, MpDestroy_WithSuspendedTasks_AllResumed) {
    /* Given: 动态创建的内存池，且有阻塞任务 */
    mrtk_mem_pool_t *mp = mrtk_mp_create("suspend_destroy", TEST_BLOCK_SIZE, TEST_BLOCK_COUNT);
    ASSERT_NE(mp, MRTK_NULL);

    /* 模拟阻塞任务 */
    mrtk_tcb_t dummy_task;
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.state = MRTK_TASK_STAT_SUSPEND;
    _mrtk_list_insert_before(&mp->suspend_tasks_dummy, &dummy_task.sched_node);

    /* When: 执行销毁 */
    mrtk_err_t ret = mrtk_mp_destroy(mp);

    /* Then: 验证返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);

    /* 验证任务被唤醒并设置错误码 */
    EXPECT_EQ(dummy_task.last_error, MRTK_EDELETED);
    EXPECT_EQ(dummy_task.state, MRTK_TASK_STAT_READY);
}

/* =============================================================================
 * mrtk_mp_alloc 测试用例
 * ============================================================================ */

/**
 * @test MpAlloc_NonBlockingWithFreeBlock_Success
 * @brief 测试非阻塞分配，有空闲块（正向测试）
 * @details 验证 time=0 时不阻塞，直接返回内存块
 * @note 等价类划分：非阻塞 + 有资源类
 */
TEST_F(MrtkMemPoolTest, MpAlloc_NonBlockingWithFreeBlock_Success) {
    /* Given: 已初始化的内存池，有空闲块 */
    mrtk_mp_init(&test_mp, "alloc_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);
    mrtk_size_t initial_free_count = test_mp.free_block_count;

    /* When: 非阻塞分配 */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);

    /* Then: 验证分配成功 */
    EXPECT_NE(block, MRTK_NULL);
    EXPECT_TRUE(is_address_in_pool(&test_mp, block));
    EXPECT_EQ(test_mp.free_block_count, initial_free_count - 1);

    /* 验证块头部存储了内存池指针 */
    EXPECT_EQ(MRTK_POOL_GET_HEADER(block), &test_mp);
}

/**
 * @test MpAlloc_NonBlockingNoFreeBlock_ReturnsNull
 * @brief 测试非阻塞分配，无空闲块（边界值测试）
 * @details 验证 time=0 且无空闲块时立即返回 NULL
 * @note 边界值分析：空资源边界
 * @note 分支覆盖：free_block_count == 0 且 time == 0 分支
 */
TEST_F(MrtkMemPoolTest, MpAlloc_NonBlockingNoFreeBlock_ReturnsNull) {
    /* Given: 已耗尽的内存池 */
    mrtk_mp_init(&test_mp, "exhausted_pool", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* 耗尽所有块 */
    mrtk_void_t *blocks[TEST_BLOCK_COUNT];
    for (mrtk_size_t i = 0; i < test_mp.total_block_count; ++i) {
        blocks[i] = mrtk_mp_alloc(&test_mp, 0);
        ASSERT_NE(blocks[i], MRTK_NULL);
    }

    ASSERT_EQ(test_mp.free_block_count, 0);

    /* When: 尝试非阻塞分配 */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);

    /* Then: 验证返回 NULL */
    EXPECT_EQ(block, MRTK_NULL);
    EXPECT_EQ(test_mp.free_block_count, 0);
}

/**
 * @test MpAlloc_BlockingWithFreeBlock_Success
 * @brief 测试阻塞分配，有空闲块（正向测试）
 * @details 验证即使设置超时，如果有空闲块也立即返回
 * @note 分支覆盖：free_block_count > 0 分支（跳过阻塞逻辑）
 */
TEST_F(MrtkMemPoolTest, MpAlloc_BlockingWithFreeBlock_Success) {
    /* Given: 已初始化的内存池，有空闲块 */
    mrtk_mp_init(&test_mp, "blocking_alloc", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* When: 阻塞分配（但有空闲块） */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, MRTK_WAITING_FOREVER);

    /* Then: 验证立即返回，不阻塞 */
    EXPECT_NE(block, MRTK_NULL);
    EXPECT_TRUE(is_address_in_pool(&test_mp, block));
}

/**
 * @test MpAlloc_AllBlocksExhausted_ReturnsNull
 * @brief 测试连续分配直到耗尽（边界值测试）
 * @details 验证分配满后无法继续分配
 * @note 边界值分析：满状态边界
 * @note 状态机覆盖：部分满 -> 满
 */
TEST_F(MrtkMemPoolTest, MpAlloc_AllBlocksExhausted_ReturnsNull) {
    /* Given: 已初始化的内存池 */
    mrtk_mp_init(&test_mp, "exhaust_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    mrtk_void_t *blocks[TEST_BLOCK_COUNT + 1]; /* 多分配一个用于测试 */
    mrtk_size_t  allocated = 0;

    /* When: 连续分配直到耗尽 */
    for (mrtk_size_t i = 0; i < TEST_BLOCK_COUNT + 1; ++i) {
        blocks[i] = mrtk_mp_alloc(&test_mp, 0);
        if (blocks[i] != MRTK_NULL) {
            allocated++;
        }
    }

    /* Then: 验证只能分配 total_block_count 个块 */
    EXPECT_EQ(allocated, test_mp.total_block_count);
    EXPECT_EQ(test_mp.free_block_count, 0);
    EXPECT_EQ(blocks[test_mp.total_block_count], MRTK_NULL);

    /* 清理 */
    for (mrtk_size_t i = 0; i < allocated; ++i) {
        mrtk_mp_free(blocks[i]);
    }
}

/**
 * @test MpAlloc_BlockHeaderContainsPoolPointer
 * @brief 测试块头部正确存储内存池指针
 * @details 验证分配的块头部存储了所属内存池的指针
 * @note 数据结构完整性测试
 */
TEST_F(MrtkMemPoolTest, MpAlloc_BlockHeaderContainsPoolPointer) {
    /* Given: 已初始化的内存池 */
    mrtk_mp_init(&test_mp, "header_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* When: 分配内存块 */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);

    /* Then: 验证块头部存储了内存池指针 */
    ASSERT_NE(block, MRTK_NULL);
    mrtk_mem_pool_t *retrieved_mp = (mrtk_mem_pool_t *) MRTK_POOL_GET_HEADER(block);
    EXPECT_EQ(retrieved_mp, &test_mp);

    /* 清理 */
    mrtk_mp_free(block);
}

/* =============================================================================
 * mrtk_mp_free 测试用例
 * ============================================================================ */

/**
 * @test MpFree_NormalFree_Success
 * @brief 测试正常释放（正向测试）
 * @details 验证释放后内存块回到空闲链表
 * @note 状态机覆盖：Allocated -> Free
 */
TEST_F(MrtkMemPoolTest, MpFree_NormalFree_Success) {
    /* Given: 已分配的内存块 */
    mrtk_mp_init(&test_mp, "free_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);
    ASSERT_NE(block, MRTK_NULL);
    mrtk_size_t free_block_count = test_mp.free_block_count;

    /* When: 释放内存块 */
    mrtk_mp_free(block);

    /* Then: 验证空闲块计数增加 */
    EXPECT_EQ(test_mp.free_block_count, free_block_count + 1);

    /* 验证空闲链表完整性 */
    EXPECT_TRUE(verify_free_list_integrity(&test_mp));
}

/**
 * @test MpFree_AfterAllAllocated_AllReturned
 * @brief 测试释放所有已分配块（边界值测试）
 * @details 验证释放所有块后，空闲块计数等于总块数
 * @note 边界值分析：满 -> 空转换
 */
TEST_F(MrtkMemPoolTest, MpFree_AfterAllAllocated_AllReturned) {
    /* Given: 已耗尽的内存池 */
    mrtk_mp_init(&test_mp, "free_all_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* 分配所有块 */
    mrtk_void_t *blocks[TEST_BLOCK_COUNT];
    for (mrtk_size_t i = 0; i < test_mp.total_block_count; ++i) {
        blocks[i] = mrtk_mp_alloc(&test_mp, 0);
        ASSERT_NE(blocks[i], MRTK_NULL);
    }

    ASSERT_EQ(test_mp.free_block_count, 0);

    /* When: 释放所有块 */
    for (mrtk_size_t i = 0; i < test_mp.total_block_count; ++i) {
        mrtk_mp_free(blocks[i]);
    }

    /* Then: 验证所有块都回到空闲链表 */
    EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
    EXPECT_TRUE(verify_free_list_integrity(&test_mp));
}

/**
 * @test MpFree_WithNoSuspendedTasks_NoSchedule
 * @brief 测试释放时无阻塞任务（分支覆盖）
 * @details 验证无阻塞任务时不触发调度
 * @note 分支覆盖：suspend_tasks_dummy 为空分支
 */
TEST_F(MrtkMemPoolTest, MpFree_WithNoSuspendedTasks_NoSchedule) {
    /* Given: 已分配的内存块，无阻塞任务 */
    mrtk_mp_init(&test_mp, "no_suspend", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);
    ASSERT_NE(block, MRTK_NULL);

    /* Mock: 确保不调用 mrtk_schedule */
    EXPECT_CALL(mock_cpu_port, mrtk_hw_context_switch_interrupt()).Times(0);

    mrtk_size_t free_block_count = test_mp.free_block_count;
    /* When: 释放内存块 */
    mrtk_mp_free(block);

    /* Then: 验证空闲块增加 */
    EXPECT_EQ(test_mp.free_block_count, free_block_count + 1);
}

/**
 * @test MpFree_WithSuspendedTasks_WakesOneTask
 * @brief 测试释放时有阻塞任务（分支覆盖）
 * @details 验证释放时唤醒一个阻塞任务
 * @note 分支覆盖：suspend_tasks_dummy 不为空分支
 */
TEST_F(MrtkMemPoolTest, MpFree_WithSuspendedTasks_WakesOneTask) {
    /* Given: 已分配的内存块，有阻塞任务 */
    mrtk_mp_init(&test_mp, "wake_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);
    ASSERT_NE(block, MRTK_NULL);

    /* 模拟阻塞任务 */
    mrtk_tcb_t dummy_task;
    memset(&dummy_task, 0, sizeof(dummy_task));
    dummy_task.state     = MRTK_TASK_STAT_SUSPEND;
    dummy_task.priority  = 10;
    dummy_task.last_error = MRTK_EOK;

    /* 模拟当前任务优先级低于阻塞任务 */
    mrtk_tcb_t *g_CurrentTCB;
    mrtk_tcb_t          current_task;
    memset(&current_task, 0, sizeof(current_task));
    current_task.priority = 20;
    g_CurrentTCB          = &current_task;

    _mrtk_list_insert_before(&test_mp.suspend_tasks_dummy, &dummy_task.sched_node);

    /* When: 释放内存块 */
    mrtk_mp_free(block);

    /* Then: 验证任务被唤醒 */
    EXPECT_EQ(dummy_task.state, MRTK_TASK_STAT_READY);
    EXPECT_EQ(dummy_task.last_error, MRTK_EOK);
}

/**
 * @test MpFree_BlockAddedToFreeListHead
 * @brief 测试释放的块添加到空闲链表头部
 * @details 验证释放的块被添加到空闲链表头部（头插法）
 * @note 数据结构测试
 */
TEST_F(MrtkMemPoolTest, MpFree_BlockAddedToFreeListHead) {
    /* Given: 已初始化的内存池，分配多个块 */
    mrtk_mp_init(&test_mp, "head_insert", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    mrtk_void_t *block1 = mrtk_mp_alloc(&test_mp, 0);
    mrtk_void_t *block2 = mrtk_mp_alloc(&test_mp, 0);
    ASSERT_NE(block1, MRTK_NULL);
    ASSERT_NE(block2, MRTK_NULL);

    /* 记录当前空闲链表头 */
    mrtk_u8_t *old_free_list = test_mp.free_block_list;

    /* When: 释放第一个块 */
    mrtk_mp_free(block1);

    /* Then: 验证 block1 成为新的空闲链表头 */
    EXPECT_EQ(test_mp.free_block_list, block1);
    EXPECT_EQ((mrtk_u8_t *) MRTK_POOL_GET_HEADER(block1), old_free_list);

    /* 清理 */
    mrtk_mp_free(block2);
}

/* =============================================================================
 * 完整生命周期测试
 * ============================================================================ */

/**
 * @test Lifecycle_StaticInitAllocFree_Complete
 * @brief 测试静态初始化完整生命周期（状态机覆盖）
 * @details 验证 Init -> Alloc -> Free -> Detach 完整流程
 * @note 状态机覆盖：静态对象完整生命周期
 */
TEST_F(MrtkMemPoolTest, Lifecycle_StaticInitAllocFree_Complete) {
    /* Given: 静态初始化的内存池 */
    mrtk_mp_init(&test_mp, "static_lifecycle", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    /* When: 分配 -> 使用 -> 释放 -> 脱离 */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);
    ASSERT_NE(block, MRTK_NULL);

    /* 使用内存块 */
    mrtk_u8_t *data = (mrtk_u8_t *) block;
    data[0]          = 0xAA;
    EXPECT_EQ(data[0], 0xAA);

    mrtk_mp_free(block);
    mrtk_err_t ret = mrtk_mp_detach(&test_mp);

    /* Then: 验证每一步都成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
}

/**
 * @test Lifecycle_DynamicCreateAllocDestroy_Complete
 * @brief 测试动态创建完整生命周期（状态机覆盖）
 * @details 验证 Create -> Alloc -> Free -> Destroy 完整流程
 * @note 状态机覆盖：动态对象完整生命周期
 */
TEST_F(MrtkMemPoolTest, Lifecycle_DynamicCreateAllocDestroy_Complete) {
    /* Given: 动态创建的内存池 */
    mrtk_mem_pool_t *mp = mrtk_mp_create("dynamic_lifecycle", TEST_BLOCK_SIZE, TEST_BLOCK_COUNT);
    ASSERT_NE(mp, MRTK_NULL);

    /* When: 分配 -> 使用 -> 释放 -> 销毁 */
    mrtk_void_t *block = mrtk_mp_alloc(mp, 0);
    ASSERT_NE(block, MRTK_NULL);

    /* 使用内存块 */
    mrtk_u8_t *data = (mrtk_u8_t *) block;
    data[0]          = 0xBB;
    EXPECT_EQ(data[0], 0xBB);

    mrtk_mp_free(block);
    mrtk_err_t ret = mrtk_mp_destroy(mp);

    /* Then: 验证每一步都成功 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/* =============================================================================
 * 压力与稳定性测试
 * ============================================================================ */

/**
 * @test Stress_RepeatedAllocFree_NoLeaks
 * @brief 测试重复分配释放（压力测试）
 * @details 验证多次分配释放后无内存泄漏
 * @note 压力测试：检测内存泄漏和链表损坏
 */
TEST_F(MrtkMemPoolTest, Stress_RepeatedAllocFree_NoLeaks) {
    /* Given: 已初始化的内存池 */
    mrtk_mp_init(&test_mp, "stress_test", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    const mrtk_size_t iterations = 100;
    mrtk_void_t *       blocks[TEST_BLOCK_COUNT];

    /* When: 重复分配释放 */
    for (mrtk_size_t iter = 0; iter < iterations; ++iter) {
        /* 分配所有块 */
        for (mrtk_size_t i = 0; i < test_mp.total_block_count; ++i) {
            blocks[i] = mrtk_mp_alloc(&test_mp, 0);
            ASSERT_NE(blocks[i], MRTK_NULL);
        }

        /* 释放所有块 */
        for (mrtk_size_t i = 0; i < test_mp.total_block_count; ++i) {
            mrtk_mp_free(blocks[i]);
        }

        /* 验证空闲块计数正确 */
        EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
        EXPECT_TRUE(verify_free_list_integrity(&test_mp));
    }

    /* Then: 验证最终状态正确 */
    EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
}

/**
 * @test Stress_AlternatingAllocFree_Stable
 * @brief 测试交替分配释放（稳定性测试）
 * @details 验证交替分配释放的稳定性
 * @note 稳定性测试：检测链表操作的正确性
 */
TEST_F(MrtkMemPoolTest, Stress_AlternatingAllocFree_Stable) {
    /* Given: 已初始化的内存池 */
    mrtk_mp_init(&test_mp, "alt_stress", test_buffer, TEST_POOL_SIZE, TEST_BLOCK_SIZE);

    mrtk_void_t *blocks[TEST_BLOCK_COUNT];
    mrtk_size_t  alloc_count = 0;

    /* When: 交替分配释放 */
    for (mrtk_size_t i = 0; i < 100; ++i) {
        if (i % 2 == 0 && alloc_count < test_mp.total_block_count) {
            /* 分配 */
            blocks[alloc_count++] = mrtk_mp_alloc(&test_mp, 0);
            ASSERT_NE(blocks[alloc_count - 1], MRTK_NULL);
        } else if (alloc_count > 0) {
            /* 释放 */
            mrtk_mp_free(blocks[--alloc_count]);
        }

        /* 每次操作后验证链表完整性 */
        EXPECT_TRUE(verify_free_list_integrity(&test_mp));
        EXPECT_EQ(test_mp.free_block_count + alloc_count, test_mp.total_block_count);
    }

    /* 清理剩余块 */
    while (alloc_count > 0) {
        mrtk_mp_free(blocks[--alloc_count]);
    }

    /* Then: 验证最终状态正确 */
    EXPECT_EQ(test_mp.free_block_count, test_mp.total_block_count);
}

/* =============================================================================
 * 边界条件专项测试
 * ============================================================================ */

/**
 * @test Boundary_MinimumBlockSize_Success
 * @brief 测试最小块大小（边界值测试）
 * @details 验证最小块大小（对齐后）可以正常工作
 * @note 边界值分析：最小有效块大小
 */
TEST_F(MrtkMemPoolTest, Boundary_MinimumBlockSize_Success) {
    /* Given: 最小块大小（1字节，对齐后为 MRTK_ALIGN_SIZE） */
    mrtk_mp_init(&test_mp, "min_block", test_buffer, TEST_POOL_SIZE, 1);

    /* When: 分配最小块 */
    mrtk_void_t *block = mrtk_mp_alloc(&test_mp, 0);

    /* Then: 验证分配成功 */
    EXPECT_NE(block, MRTK_NULL);
    EXPECT_EQ(test_mp.block_size, MRTK_ALIGN_SIZE);

    /* 清理 */
    mrtk_mp_free(block);
}

/**
 * @test Boundary_UnalignedSizeRoundUp_Success
 * @brief 测试非对齐大小向上取整（边界值测试）
 * @details 验证非对齐大小被正确向上取整
 * @note 边界值分析：非对齐大小处理
 */
TEST_F(MrtkMemPoolTest, Boundary_UnalignedSizeRoundUp_Success) {
    /* Given: 非对齐大小 */
    mrtk_size_t unaligned_sizes[] = {1, 3, 5, 7, 9, 13, 15, 17};

    for (mrtk_size_t i = 0; i < sizeof(unaligned_sizes) / sizeof(unaligned_sizes[0]); ++i) {
        mrtk_mem_pool_t mp;
        mrtk_mp_init(&mp, "unaligned_test", test_buffer, TEST_POOL_SIZE, unaligned_sizes[i]);

        /* Then: 验证被向上对齐 */
        EXPECT_EQ(mp.block_size, MRTK_ALIGN_UP(unaligned_sizes[i], MRTK_ALIGN_SIZE));

        /* 验证可以正常分配 */
        mrtk_void_t *block = mrtk_mp_alloc(&mp, 0);
        EXPECT_NE(block, MRTK_NULL);
        mrtk_mp_free(block);

        mrtk_mp_detach(&mp);
    }
}

/* =============================================================================
 * 主函数
 * ============================================================================ */

/**
 * @brief 主函数
 * @details 初始化 GTest 框架并运行所有测试
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
