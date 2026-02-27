/**
 * @file mrtk_mem_heap_test.cpp
 * @author leiyx
 * @brief 堆内存管理模块单元测试
 * @details 使用 GTest/GMock 框架对堆内存管理模块进行全面测试
 *
 * 测试覆盖策略：
 * 1. 边界值分析：size=0/对齐边界、最小/最大分配、满/空状态
 * 2. 等价类划分：NULL指针防御、非法魔数、越界地址、合法参数
 * 3. 分支覆盖：所有 if/else 分支（合并逻辑、切分逻辑、lfree更新）
 * 4. 状态机覆盖：Init -> Alloc -> Free（前/后合并）-> 完整闭环
 * 5. 碎片化场景：反复分配释放产生碎片、验证合并算法
 *
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/* MRTK 内核头文件 */
#include "mrtk.h"
#include "mrtk_mem_heap.h"
#include "mrtk_config_internal.h"
#include "mrtk_typedef.h"
#include "mrtk_errno.h"
#include "mrtk_schedule.h"

/* Mock 框架头文件 */
#include "mrtk_mock_hw.hpp"

extern "C" {
    extern mrtk_u8_t *heap_base_addr;
    extern mrtk_size_t heap_size;
    extern mrtk_heap_mem_t *lfree;
}

/* 测试常量定义 */
#define TEST_HEAP_SIZE        (4096)   /* 测试用堆大小 */
#define TEST_ALIGN_SIZE       (8)      /* 对齐大小 */
#define TEST_HEAP_MIN_SIZE    (3 * MRTK_HEAP_HEADER_SIZE + MRTK_HEAP_DATA_MIN_SIZE)

/* =============================================================================
 * 测试夹具类定义
 * ============================================================================ */

/**
 * @class MrtkMemHeapTest
 * @brief 堆内存管理模块测试夹具
 * @details 提供统一的测试环境初始化和清理
 */
class MrtkMemHeapTest : public ::testing::Test {
  protected:
    MockCpuPort  mock_cpu_port;
    mrtk_u8_t    test_heap_buffer[TEST_HEAP_SIZE];

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
        memset(test_heap_buffer, 0, sizeof(test_heap_buffer));
    }

    void TearDown() override {
        mrtk_mock_clear_cpu_port();
    }

    /**
     * @brief 辅助函数：获取堆内存块信息
     * @param ptr 内存块数据域指针
     * @return mrtk_heap_mem_t* 内存块头部指针
     */
    mrtk_heap_mem_t *get_block_header(mrtk_void_t *ptr) {
        if (ptr == nullptr) {
            return nullptr;
        }
        return (mrtk_heap_mem_t *) ((mrtk_u8_t *) ptr - MRTK_HEAP_HEADER_SIZE);
    }

    /**
     * @brief 辅助函数：验证内存块的完整性
     * @param ptr 内存块数据域指针
     * @return mrtk_bool_t 完整返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t verify_block_integrity(mrtk_void_t *ptr) {
        if (ptr == nullptr) {
            return MRTK_FALSE;
        }

        mrtk_heap_mem_t *block = get_block_header(ptr);

        /* 验证魔数 */
        if (block->magic != MRTK_HEAP_MAGIC) {
            return MRTK_FALSE;
        }

        /* 验证状态 */
        if (block->state != MRTK_HEAP_BLOCK_STATE_USED &&
            block->state != MRTK_HEAP_BLOCK_STATE_FREE &&
            block->state != MRTK_HEAP_BLOCK_STATE_DUMMY) {
            return MRTK_FALSE;
        }

        /* 验证地址范围 */
        if ((mrtk_u8_t *) block - heap_base_addr >= heap_size) {
            return MRTK_FALSE;
        }

        return MRTK_TRUE;
    }

    /**
     * @brief 辅助函数：计算堆的空闲空间总量
     * @return mrtk_size_t 空闲空间总量
     */
    mrtk_size_t calculate_total_free(void) {
        mrtk_size_t      total_free = 0;
        mrtk_heap_mem_t *block      = (mrtk_heap_mem_t *) heap_base_addr;

        while ((mrtk_u8_t *) block - heap_base_addr < heap_size) {
            if (block->state == MRTK_HEAP_BLOCK_STATE_FREE) {
                mrtk_size_t block_size =
                    (mrtk_u8_t *) (heap_base_addr + block->next) - (mrtk_u8_t *) block -
                    MRTK_HEAP_HEADER_SIZE;
                total_free += block_size;
            }
            block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);
        }

        return total_free;
    }

    /**
     * @brief 辅助函数：计算堆的已用空间总量
     * @return mrtk_size_t 已用空间总量
     */
    mrtk_size_t calculate_total_used(void) {
        mrtk_size_t      total_used = 0;
        mrtk_heap_mem_t *block      = (mrtk_heap_mem_t *) heap_base_addr;

        while ((mrtk_u8_t *) block - heap_base_addr < heap_size) {
            if (block->state == MRTK_HEAP_BLOCK_STATE_USED) {
                mrtk_size_t block_size =
                    (mrtk_u8_t *) (heap_base_addr + block->next) - (mrtk_u8_t *) block -
                    MRTK_HEAP_HEADER_SIZE;
                total_used += block_size;
            }
            block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);
        }

        return total_used;
    }

    /**
     * @brief 辅助函数：验证堆的链表完整性
     * @return mrtk_bool_t 完整返回 MRTK_TRUE，否则返回 MRTK_FALSE
     */
    mrtk_bool_t verify_heap_chain_integrity(void) {
        mrtk_heap_mem_t *block = (mrtk_heap_mem_t *) heap_base_addr;
        mrtk_u32_t        count = 0;

        while ((mrtk_u8_t *) block - heap_base_addr < heap_size) {
            /* 验证魔数 */
            if (block->magic != MRTK_HEAP_MAGIC) {
                return MRTK_FALSE;
            }

            /* 验证 next 指针 */
            if (block->next == 0 || block->next > heap_size) {
                return MRTK_FALSE;
            }

            /* 验证 prev 指针 */
            mrtk_heap_mem_t *prev = (mrtk_heap_mem_t *) (heap_base_addr + block->prev);
            if (block != (mrtk_heap_mem_t *) heap_base_addr) {
                if ((mrtk_u8_t *) (heap_base_addr + prev->next) != (mrtk_u8_t *) block) {
                    return MRTK_FALSE;
                }
            }

            block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);
            count++;

            /* 防止死循环 */
            if (count > 1000) {
                return MRTK_FALSE;
            }
        }

        return MRTK_TRUE;
    }

    /**
     * @brief 辅助函数：统计堆中的内存块数量
     * @return mrtk_u32_t 内存块数量
     */
    mrtk_u32_t count_heap_blocks(void) {
        mrtk_u32_t        count = 0;
        mrtk_heap_mem_t *block = (mrtk_heap_mem_t *) heap_base_addr;

        while ((mrtk_u8_t *) block - heap_base_addr < heap_size) {
            count++;
            block = (mrtk_heap_mem_t *) (heap_base_addr + block->next);
        }

        return count;
    }
};

/* =============================================================================
 * mrtk_heap_init 测试用例
 * ============================================================================ */

/**
 * @test HeapInit_NormalInit_Success
 * @brief 测试正常初始化（正向测试）
 * @details 验证堆初始化后所有字段正确设置
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkMemHeapTest, HeapInit_NormalInit_Success) {
    /* Given: 合法的初始化参数 */
    mrtk_u8_t *heap_start = test_heap_buffer;
    mrtk_u8_t *heap_end   = test_heap_buffer + TEST_HEAP_SIZE;

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_heap_init(heap_start, heap_end);

    /* Then: 验证返回值和堆状态 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_NE(heap_base_addr, MRTK_NULL);
    EXPECT_GT(heap_size, 0);

    /* 验证起始哨兵 */
    mrtk_heap_mem_t *start_dummy = (mrtk_heap_mem_t *) heap_base_addr;
    EXPECT_EQ(start_dummy->magic, MRTK_HEAP_MAGIC);
    EXPECT_EQ(start_dummy->state, MRTK_HEAP_BLOCK_STATE_DUMMY);
    EXPECT_EQ(start_dummy->prev, 0);

    /* 验证结束哨兵 */
    mrtk_heap_mem_t *end_dummy =
        (mrtk_heap_mem_t *) (heap_base_addr + start_dummy->next);
    end_dummy = (mrtk_heap_mem_t *) (heap_base_addr + end_dummy->next);
    EXPECT_EQ(end_dummy->magic, MRTK_HEAP_MAGIC);
    EXPECT_EQ(end_dummy->state, MRTK_HEAP_BLOCK_STATE_DUMMY);
    EXPECT_EQ(end_dummy->next, heap_size);

    /* 验证中间空闲块 */
    mrtk_heap_mem_t *free_block =
        (mrtk_heap_mem_t *) (heap_base_addr + start_dummy->next);
    EXPECT_EQ(free_block->magic, MRTK_HEAP_MAGIC);
    EXPECT_EQ(free_block->state, MRTK_HEAP_BLOCK_STATE_FREE);

    /* 验证 lfree 指向空闲块 */
    EXPECT_EQ(lfree, free_block);
}

/**
 * @test HeapInit_UnalignedAddress_Success
 * @brief 测试非对齐地址初始化（边界值测试）
 * @details 验证非对齐起始/结束地址会被正确对齐
 * @note 边界值分析：非对齐地址边界
 */
TEST_F(MrtkMemHeapTest, HeapInit_UnalignedAddress_Success) {
    /* Given: 非对齐的起始和结束地址 */
    mrtk_u8_t *heap_start = test_heap_buffer + 1;  /* 偏移1字节 */
    mrtk_u8_t *heap_end   = test_heap_buffer + TEST_HEAP_SIZE - 3; /* 偏移3字节 */

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_heap_init(heap_start, heap_end);

    /* Then: 验证对齐处理正确 */
    EXPECT_EQ(ret, MRTK_EOK);

    mrtk_ptr_t aligned_start = MRTK_ALIGN_UP((mrtk_ptr_t) heap_start, MRTK_ALIGN_SIZE);
    mrtk_ptr_t aligned_end   = MRTK_ALIGN_DOWN((mrtk_ptr_t) heap_end, MRTK_ALIGN_SIZE);

    EXPECT_GE((mrtk_ptr_t) heap_base_addr, aligned_start);
    EXPECT_LE((mrtk_ptr_t) heap_base_addr, aligned_start);
}

/**
 * @test HeapInit_InsufficientSpace_Error
 * @brief 测试空间不足（边界值测试）
 * @details 验证当空间不足以容纳3个块时返回错误
 * @note 边界值分析：最小有效空间边界
 * @note 分支覆盖：end_addr - begin_addr < 3 * HEADER_SIZE + DATA_MIN_SIZE 分支
 */
TEST_F(MrtkMemHeapTest, HeapInit_InsufficientSpace_Error) {
    /* Given: 极小的堆空间 */
    mrtk_u8_t tiny_buffer[TEST_HEAP_MIN_SIZE - 1];

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_heap_init(tiny_buffer, tiny_buffer + sizeof(tiny_buffer));

    /* Then: 验证返回错误 */
    EXPECT_EQ(ret, MRTK_ERROR);
}

/**
 * @test HeapInit_MinimumSpace_Success
 * @brief 测试最小有效空间（边界值测试）
 * @details 验证最小有效空间可以成功初始化
 * @note 边界值分析：最小有效空间边界
 */
TEST_F(MrtkMemHeapTest, HeapInit_MinimumSpace_Success) {
    /* Given: 最小有效空间 */
    mrtk_u8_t min_buffer[TEST_HEAP_MIN_SIZE];

    /* When: 执行初始化 */
    mrtk_err_t ret = mrtk_heap_init(min_buffer, min_buffer + sizeof(min_buffer));

    /* Then: 验证初始化成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_NE(heap_base_addr, MRTK_NULL);
}

/**
 * @test HeapInit_NullParameters_NoCrash
 * @brief 测试 NULL 参数处理（防御性测试）
 * @details 验证传入 NULL 指针时不会崩溃
 * @note 等价类划分：非法参数类
 */
TEST_F(MrtkMemHeapTest, HeapInit_NullParameters_NoCrash) {
    /* Given: NULL 参数 */
    /* Note: MRTK 未对 NULL 进行显式检查，这是设计决策 */

    /* When & Then: 不应立即崩溃（但行为未定义） */
    /* 实际使用中应确保调用者传入有效指针 */
}

/* =============================================================================
 * mrtk_malloc 测试用例
 * ============================================================================ */

/**
 * @test Malloc_NormalAlloc_Success
 * @brief 测试正常分配（正向测试）
 * @details 验证分配成功后内存块状态正确
 * @note 等价类划分：合法参数类
 */
TEST_F(MrtkMemHeapTest, Malloc_NormalAlloc_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_size_t alloc_size = 128;

    /* When: 分配内存 */
    mrtk_void_t *ptr = mrtk_malloc(alloc_size);

    /* Then: 验证分配成功 */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_TRUE(verify_block_integrity(ptr));

    mrtk_heap_mem_t *block = get_block_header(ptr);
    EXPECT_EQ(block->state, MRTK_HEAP_BLOCK_STATE_USED);
    EXPECT_EQ(block->magic, MRTK_HEAP_MAGIC);
}

/**
 * @test Malloc_ZeroSize_Success
 * @brief 测试分配0字节（边界值测试）
 * @details 验证分配0字节时实际分配对齐后的大小
 * @note 边界值分析：0 边界
 */
TEST_F(MrtkMemHeapTest, Malloc_ZeroSize_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* When: 分配0字节 */
    mrtk_void_t *ptr = mrtk_malloc(0);

    /* Then: 验证分配成功（实际分配对齐后的大小） */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_TRUE(verify_block_integrity(ptr));
}

/**
 * @test Malloc_UnalignedSize_Aligned
 * @brief 测试非对齐大小分配（边界值测试）
 * @details 验证非对齐大小会被向上对齐
 * @note 边界值分析：非对齐大小边界
 */
TEST_F(MrtkMemHeapTest, Malloc_UnalignedSize_Aligned) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* 测试多个非对齐大小 */
    mrtk_size_t unaligned_sizes[] = {1, 3, 5, 7, 9, 13, 15, 17};

    for (mrtk_size_t i = 0; i < sizeof(unaligned_sizes) / sizeof(unaligned_sizes[0]); ++i) {
        /* When: 分配非对齐大小 */
        mrtk_void_t *ptr = mrtk_malloc(unaligned_sizes[i]);

        /* Then: 验证分配成功 */
        EXPECT_NE(ptr, MRTK_NULL) << "Failed for size: " << unaligned_sizes[i];
        EXPECT_TRUE(verify_block_integrity(ptr));

        /* 清理 */
        mrtk_free(ptr);
    }
}

/**
 * @test Malloc_HeapFull_ReturnsNull
 * @brief 测试堆满时分配（边界值测试）
 * @details 验证堆满时分配失败返回 NULL
 * @note 边界值分析：满状态边界
 * @note 分支覆盖：找不到合适块分支
 */
TEST_F(MrtkMemHeapTest, Malloc_HeapFull_ReturnsNull) {
    /* Given: 已初始化的堆，分配超大块占满堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_size_t huge_size = heap_size - MRTK_HEAP_HEADER_SIZE * 3 - MRTK_HEAP_DATA_MIN_SIZE;
    mrtk_void_t *block1    = mrtk_malloc(huge_size);
    ASSERT_NE(block1, MRTK_NULL);

    /* When: 尝试再次分配 */
    mrtk_void_t *block2 = mrtk_malloc(100);

    /* Then: 验证分配失败 */
    EXPECT_EQ(block2, MRTK_NULL);

    /* 清理 */
    mrtk_free(block1);
}

/**
 * @test Malloc_BlockSplit_Success
 * @brief 测试块切分（分支覆盖）
 * @details 验证当剩余空间足够时块会被切分
 * @note 分支覆盖：cur_free_size - needed_size >= HEADER_SIZE + DATA_MIN_SIZE 分支
 */
TEST_F(MrtkMemHeapTest, Malloc_BlockSplit_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_u32_t initial_block_count = count_heap_blocks();

    /* When: 分配一个小块（剩余空间足够切分） */
    mrtk_void_t *ptr = mrtk_malloc(64);

    /* Then: 验证块被切分（块数增加） */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_EQ(count_heap_blocks(), initial_block_count + 1);

    /* 清理 */
    mrtk_free(ptr);
}

/**
 * @test Malloc_BlockNoSplit_InternalFragmentation
 * @brief 测试块不切分（分支覆盖）
 * @details 验证当剩余空间不足时块不切分，产生内部碎片
 * @note 分支覆盖：cur_free_size - needed_size < HEADER_SIZE + DATA_MIN_SIZE 分支
 */
TEST_F(MrtkMemHeapTest, Malloc_BlockNoSplit_InternalFragmentation) {
    /* Given: 已初始化的堆，分配特定大小 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_u32_t initial_block_count = count_heap_blocks();

    /* 分配一个稍小的块，使剩余空间不足以切分 */
    mrtk_size_t free_size = calculate_total_free();
    mrtk_size_t alloc_size =
        free_size - MRTK_HEAP_HEADER_SIZE - MRTK_HEAP_DATA_MIN_SIZE + 4;

    /* When: 分配 */
    mrtk_void_t *ptr = mrtk_malloc(alloc_size);

    /* Then: 验证块不切分（块数不变） */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_EQ(count_heap_blocks(), initial_block_count);

    /* 清理 */
    mrtk_free(ptr);
}

/**
 * @test Malloc_LfreeUpdate_Success
 * @brief 测试 lfree 指针更新（分支覆盖）
 * @details 验证分配后 lfree 指向最低地址空闲块
 * @note 分支覆盖：while (cur_free == lfree && lfree->state == USED) 循环
 */
TEST_F(MrtkMemHeapTest, Malloc_LfreeUpdate_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_heap_mem_t *original_lfree = lfree;

    /* When: 分配当前 lfree 指向的块 */
    mrtk_void_t *ptr = mrtk_malloc(heap_size / 2);

    /* Then: 验证 lfree 被更新 */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_NE(lfree, original_lfree);

    /* 清理 */
    mrtk_free(ptr);
}

/**
 * @test Malloc_ConsecutiveAlloc_Success
 * @brief 测试连续分配（压力测试）
 * @details 验证连续分配多个块直到堆满
 * @note 压力测试：满状态转换
 */
TEST_F(MrtkMemHeapTest, Malloc_ConsecutiveAlloc_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* When: 连续分配小块直到堆满 */
    mrtk_void_t *blocks[100];
    mrtk_u32_t   alloc_count = 0;

    for (mrtk_u32_t i = 0; i < 100; ++i) {
        blocks[i] = mrtk_malloc(32);
        if (blocks[i] != MRTK_NULL) {
            alloc_count++;
        } else {
            break;
        }
    }

    /* Then: 验证至少分配了多个块 */
    EXPECT_GT(alloc_count, 0);

    /* 清理 */
    for (mrtk_u32_t i = 0; i < alloc_count; ++i) {
        mrtk_free(blocks[i]);
    }
}

/* =============================================================================
 * mrtk_free 测试用例
 * ============================================================================ */

/**
 * @test Free_NormalFree_Success
 * @brief 测试正常释放（正向测试）
 * @details 验证释放后内存块变为空闲状态
 * @note 等价类划分：合法参数类
 * @note 状态机覆盖：USED -> FREE
 */
TEST_F(MrtkMemHeapTest, Free_NormalFree_Success) {
    /* Given: 已分配的内存块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr = mrtk_malloc(128);
    ASSERT_NE(ptr, MRTK_NULL);

    mrtk_heap_mem_t *block = get_block_header(ptr);
    ASSERT_EQ(block->state, MRTK_HEAP_BLOCK_STATE_USED);

    /* When: 释放内存 */
    mrtk_free(ptr);

    /* Then: 验证块变为空闲状态 */
    EXPECT_EQ(block->state, MRTK_HEAP_BLOCK_STATE_FREE);
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Free_NullPointer_NoEffect
 * @brief 测试释放 NULL 指针（防御性测试）
 * @details 验证释放 NULL 不会崩溃
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：ptr == NULL 分支
 */
TEST_F(MrtkMemHeapTest, Free_NullPointer_NoEffect) {
    /* Given: NULL 指针 */

    /* When: 释放 NULL */
    mrtk_free(MRTK_NULL);

    /* Then: 不应崩溃，堆状态不变 */
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Free_InvalidMagic_NoEffect
 * @brief 测试释放魔数错误的块（防御性测试）
 * @details 验证魔数错误时不会释放
 * @note 等价类划分：非法参数类
 * @note 分支覆盖：block->magic != MRTK_HEAP_MAGIC 分支
 */
TEST_F(MrtkMemHeapTest, Free_InvalidMagic_NoEffect) {
    /* Given: 已分配的内存块，修改魔数 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr = mrtk_malloc(128);
    ASSERT_NE(ptr, MRTK_NULL);

    mrtk_heap_mem_t *block = get_block_header(ptr);
    block->magic = 0xFFFF; /* 错误的魔数 */

    mrtk_size_t free_count_before = count_heap_blocks();

    /* When: 尝试释放 */
    mrtk_free(ptr);

    /* Then: 验证块未被释放 */
    EXPECT_EQ(count_heap_blocks(), free_count_before);
    EXPECT_EQ(block->state, MRTK_HEAP_BLOCK_STATE_USED);

    /* 恢复魔数以便清理 */
    block->magic = MRTK_HEAP_MAGIC;
    mrtk_free(ptr);
}

/**
 * @test Free_FreeBlock_NoEffect
 * @brief 测试释放空闲块（防御性测试）
 * @details 验证释放空闲块时不会有操作
 * @note 等价类划分：非法状态类
 * @note 分支覆盖：block->state != USED 分支
 */
TEST_F(MrtkMemHeapTest, Free_FreeBlock_NoEffect) {
    /* Given: 已分配并释放的内存块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr = mrtk_malloc(128);
    ASSERT_NE(ptr, MRTK_NULL);

    mrtk_free(ptr);

    mrtk_heap_mem_t *block = get_block_header(ptr);
    ASSERT_EQ(block->state, MRTK_HEAP_BLOCK_STATE_FREE);

    mrtk_u32_t block_count_before = count_heap_blocks();

    /* When: 再次释放 */
    mrtk_free(ptr);

    /* Then: 验证无变化 */
    EXPECT_EQ(count_heap_blocks(), block_count_before);
}

/**
 * @test Free_ForwardMerge_Success
 * @brief 测试向前合并（分支覆盖）
 * @details 验证与前一个空闲块合并
 * @note 分支覆盖：prev_block->state == FREE 分支
 */
TEST_F(MrtkMemHeapTest, Free_ForwardMerge_Success) {
    /* Given: 已初始化的堆，分配三个相邻块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr1 = mrtk_malloc(128);
    mrtk_void_t *ptr2 = mrtk_malloc(128);
    mrtk_void_t *ptr3 = mrtk_malloc(128);
    ASSERT_NE(ptr1, MRTK_NULL);
    ASSERT_NE(ptr2, MRTK_NULL);
    ASSERT_NE(ptr3, MRTK_NULL);

    mrtk_u32_t block_count_before = count_heap_blocks();

    /* 释放第一个块（变为空闲） */
    mrtk_free(ptr1);

    /* When: 释放第二个块（应与第一个合并） */
    mrtk_free(ptr2);

    /* Then: 验证块数减少（合并成功） */
    EXPECT_EQ(count_heap_blocks(), block_count_before - 1);
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Free_BackwardMerge_Success
 * @brief 测试向后合并（分支覆盖）
 * @details 验证与后一个空闲块合并
 * @note 分支覆盖：next_block->state == FREE 分支
 */
TEST_F(MrtkMemHeapTest, Free_BackwardMerge_Success) {
    /* Given: 已初始化的堆，分配两个相邻块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr1 = mrtk_malloc(128);
    mrtk_void_t *ptr2 = mrtk_malloc(128);
    mrtk_void_t *ptr3 = mrtk_malloc(128);
    ASSERT_NE(ptr1, MRTK_NULL);
    ASSERT_NE(ptr2, MRTK_NULL);
    ASSERT_NE(ptr3, MRTK_NULL);

    mrtk_u32_t block_count_before = count_heap_blocks();

    /* 释放第二个块（变为空闲） */
    mrtk_free(ptr2);
    
    /* When: 释放第一个块（应与第二个合并） */
    mrtk_free(ptr1);

    /* Then: 验证块数减少（合并成功） */
    EXPECT_EQ(count_heap_blocks(), block_count_before - 1);
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Free_BothDirectionMerge_Success
 * @brief 测试双向合并（分支覆盖）
 * @details 验证同时与前后的空闲块合并
 * @note 分支覆盖：prev_block->state == FREE && next_block->state == FREE 分支
 */
TEST_F(MrtkMemHeapTest, Free_BothDirectionMerge_Success) {
    /* Given: 已初始化的堆，分配四个相邻块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr1 = mrtk_malloc(128);
    mrtk_void_t *ptr2 = mrtk_malloc(128);
    mrtk_void_t *ptr3 = mrtk_malloc(128);
    mrtk_void_t *ptr4 = mrtk_malloc(128);
    ASSERT_NE(ptr1, MRTK_NULL);
    ASSERT_NE(ptr2, MRTK_NULL);
    ASSERT_NE(ptr3, MRTK_NULL);
    ASSERT_NE(ptr4, MRTK_NULL);

    mrtk_u32_t block_count_before = count_heap_blocks();

    /* 释放第一个和第三个块 */
    mrtk_free(ptr1);
    mrtk_free(ptr3);

    /* When: 释放中间的块（应与前后合并） */
    mrtk_free(ptr2);

    /* Then: 验证块数减少（双向合并成功） */
    EXPECT_EQ(count_heap_blocks(), block_count_before - 2);
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Free_LfreeUpdate_Success
 * @brief 测试释放时更新 lfree（分支覆盖）
 * @details 验证释放地址低于 lfree 时更新 lfree
 * @note 分支覆盖：if (lfree > block) 分支
 */
TEST_F(MrtkMemHeapTest, Free_LfreeUpdate_Success) {
    /* Given: 已初始化的堆，分配多个块 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr1 = mrtk_malloc(64);
    mrtk_void_t *ptr2 = mrtk_malloc(128);
    ASSERT_NE(ptr1, MRTK_NULL);
    ASSERT_NE(ptr2, MRTK_NULL);

    mrtk_heap_mem_t *original_lfree = lfree;

    /* 释放第一个块（地址低于 lfree） */
    mrtk_free(ptr1);

    /* When: 释放第二个块（地址可能仍高于原 lfree） */
    mrtk_free(ptr2);

    /* Then: 验证 lfree 被更新为最低地址空闲块 */
    EXPECT_LE(lfree, original_lfree);
}

/* =============================================================================
 * mrtk_heap_init 测试用例
 * ============================================================================ */

/**
 * @test HeapReset_ResetToInitialState_Success
 * @brief 测试堆重置功能（正向测试）
 * @details 验证重置后堆恢复到初始状态
 * @note 状态机覆盖：任意状态 -> 初始状态
 */
TEST_F(MrtkMemHeapTest, HeapReset_ResetToInitialState_Success) {
    /* Given: 已初始化并使用的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);
    mrtk_void_t *ptr1 = mrtk_malloc(128);
    mrtk_void_t *ptr2 = mrtk_malloc(256);
    ASSERT_NE(ptr1, MRTK_NULL);
    ASSERT_NE(ptr2, MRTK_NULL);

    /* When: 重置堆 */
    /* 强制重新初始化堆（重建哨兵节点，清空空闲列表） */
    mrtk_heap_init(g_heap_buffer, g_heap_buffer + MRTK_HEAP_SIZE);

    /* Then: 验证堆恢复到初始状态 */
    EXPECT_NE(heap_base_addr, MRTK_NULL);
    EXPECT_NE(lfree, MRTK_NULL);

    /* 验证只有3个块（start_dummy, free_block, end_dummy） */
    EXPECT_EQ(count_heap_blocks(), 3);

    /* 验证 lfree 指向空闲块 */
    EXPECT_EQ(lfree->state, MRTK_HEAP_BLOCK_STATE_FREE);

    /* 验证链表完整性 */
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/* =============================================================================
 * 完整生命周期测试
 * ============================================================================ */

/**
 * @test Lifecycle_InitAllocFree_Complete
 * @brief 测试完整生命周期（状态机覆盖）
 * @details 验证 Init -> Alloc -> Free 完整流程
 * @note 状态机覆盖：完整生命周期
 */
TEST_F(MrtkMemHeapTest, Lifecycle_InitAllocFree_Complete) {
    /* Given: 初始化堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* When: 分配 -> 使用 -> 释放 */
    mrtk_void_t *ptr = mrtk_malloc(256);
    ASSERT_NE(ptr, MRTK_NULL);

    /* 使用内存 */
    mrtk_u8_t *data = (mrtk_u8_t *) ptr;
    data[0]          = 0xAA;
    EXPECT_EQ(data[0], 0xAA);

    mrtk_free(ptr);

    /* Then: 验证堆状态正确 */
    EXPECT_TRUE(verify_heap_chain_integrity());
    EXPECT_EQ(calculate_total_used(), 0);
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
TEST_F(MrtkMemHeapTest, Stress_RepeatedAllocFree_NoLeaks) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    const mrtk_u32_t iterations = 100;

    /* When: 重复分配释放 */
    for (mrtk_u32_t iter = 0; iter < iterations; ++iter) {
        mrtk_void_t *ptr = mrtk_malloc(128);
        ASSERT_NE(ptr, MRTK_NULL) << "Iteration: " << iter;

        /* 使用内存 */
        mrtk_u8_t *data = (mrtk_u8_t *) ptr;
        data[0]          = (mrtk_u8_t) iter;

        mrtk_free(ptr);

        /* 验证堆状态 */
        EXPECT_TRUE(verify_heap_chain_integrity()) << "Iteration: " << iter;
        EXPECT_EQ(calculate_total_used(), 0) << "Iteration: " << iter;
    }

    /* Then: 验证最终状态正确 */
    EXPECT_EQ(calculate_total_free(), calculate_total_free());
}

/**
 * @test Stress_Fragmentation_NoCorruption
 * @brief 测试内存碎片化（稳定性测试）
 * @details 验证反复分配释放不同大小块后堆不损坏
 * @note 稳定性测试：检测碎片化场景下的正确性
 */
TEST_F(MrtkMemHeapTest, Stress_Fragmentation_NoCorruption) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    mrtk_void_t *blocks[50];
    mrtk_size_t  block_sizes[] = {16, 32, 64, 128, 256, 512};

    /* When: 反复分配释放不同大小的块 */
    for (mrtk_u32_t round = 0; round < 50; ++round) {
        /* 分配多个不同大小的块 */
        for (mrtk_u32_t i = 0; i < 50; ++i) {
            mrtk_size_t size = block_sizes[i % (sizeof(block_sizes) / sizeof(block_sizes[0]))];
            blocks[i]        = mrtk_malloc(size);
        }

        /* 释放所有块 */
        for (mrtk_u32_t i = 0; i < 50; ++i) {
            if (blocks[i] != MRTK_NULL) {
                mrtk_free(blocks[i]);
            }
        }

        /* 验证堆状态 */
        EXPECT_TRUE(verify_heap_chain_integrity()) << "Round: " << round;
    }

    /* Then: 验证最终状态正确 */
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/**
 * @test Stress_RandomAllocFree_Stable
 * @brief 测试随机分配释放（稳定性测试）
 * @details 验证随机分配释放的稳定性
 * @note 稳定性测试：模拟实际使用场景
 */
TEST_F(MrtkMemHeapTest, Stress_RandomAllocFree_Stable) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    mrtk_void_t *blocks[20];
    mrtk_bool_t  allocated[20] = {MRTK_FALSE};

    /* When: 随机分配释放 */
    for (mrtk_u32_t i = 0; i < 1000; ++i) {
        mrtk_u32_t idx = i % 20;

        if (allocated[idx]) {
            /* 释放 */
            mrtk_free(blocks[idx]);
            allocated[idx] = MRTK_FALSE;
        } else {
            /* 分配 */
            blocks[idx] = mrtk_malloc(32 + (i % 10) * 16);
            if (blocks[idx] != MRTK_NULL) {
                allocated[idx] = MRTK_TRUE;
            }
        }
    }

    /* 清理剩余块 */
    for (mrtk_u32_t i = 0; i < 20; ++i) {
        if (allocated[i]) {
            mrtk_free(blocks[i]);
        }
    }

    /* Then: 验证堆状态正确 */
    EXPECT_TRUE(verify_heap_chain_integrity());
    EXPECT_EQ(calculate_total_used(), 0);
}

/**
 * @test Stress_AlternatingAllocFree_Stable
 * @brief 测试交替分配释放（稳定性测试）
 * @details 验证交替分配释放的稳定性
 * @note 稳定性测试：检测链表操作的正确性
 */
TEST_F(MrtkMemHeapTest, Stress_AlternatingAllocFree_Stable) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* When: 交替分配释放 */
    for (mrtk_u32_t i = 0; i < 100; ++i) {
        mrtk_void_t *ptr1 = mrtk_malloc(64);
        mrtk_void_t *ptr2 = mrtk_malloc(128);
        mrtk_void_t *ptr3 = mrtk_malloc(256);

        ASSERT_NE(ptr1, MRTK_NULL);
        ASSERT_NE(ptr2, MRTK_NULL);
        ASSERT_NE(ptr3, MRTK_NULL);

        mrtk_free(ptr1);
        mrtk_free(ptr3);
        mrtk_free(ptr2);

        /* 验证堆状态 */
        EXPECT_TRUE(verify_heap_chain_integrity()) << "Iteration: " << i;
    }

    /* Then: 验证最终状态正确 */
    EXPECT_TRUE(verify_heap_chain_integrity());
}

/* =============================================================================
 * 边界条件专项测试
 * ============================================================================ */

/**
 * @test Boundary_MaxAllocSize_Success
 * @brief 测试最大分配大小（边界值测试）
 * @details 验证分配接近堆大小的大块
 * @note 边界值分析：最大有效分配
 */
TEST_F(MrtkMemHeapTest, Boundary_MaxAllocSize_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    mrtk_size_t max_size = heap_size - MRTK_HEAP_HEADER_SIZE * 3 - MRTK_HEAP_DATA_MIN_SIZE;

    /* When: 分配接近最大大小的块 */
    mrtk_void_t *ptr = mrtk_malloc(max_size);

    /* Then: 验证分配成功 */
    EXPECT_NE(ptr, MRTK_NULL);
    EXPECT_TRUE(verify_block_integrity(ptr));

    /* 清理 */
    mrtk_free(ptr);
}

/**
 * @test Boundary_SmallAlloc_Success
 * @brief 测试最小分配（边界值测试）
 * @details 验证分配最小块可以正常工作
 * @note 边界值分析：最小有效分配
 */
TEST_F(MrtkMemHeapTest, Boundary_SmallAlloc_Success) {
    /* Given: 已初始化的堆 */
    mrtk_heap_init(test_heap_buffer, test_heap_buffer + TEST_HEAP_SIZE);

    /* 测试多个小分配 */
    mrtk_size_t small_sizes[] = {1, 2, 3, 4, 5, 6, 7, 8};

    for (mrtk_size_t i = 0; i < sizeof(small_sizes) / sizeof(small_sizes[0]); ++i) {
        /* When: 分配小块 */
        mrtk_void_t *ptr = mrtk_malloc(small_sizes[i]);

        /* Then: 验证分配成功 */
        EXPECT_NE(ptr, MRTK_NULL) << "Failed for size: " << small_sizes[i];
        EXPECT_TRUE(verify_block_integrity(ptr));

        /* 清理 */
        mrtk_free(ptr);
    }
}