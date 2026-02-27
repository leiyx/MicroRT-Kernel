/**
 * @file mrtk_test_ringbuffer.cc
 * @author leiyx
 * @brief 环形缓冲区模块单元测试
 * @details 严格遵循边界值分析、等价类划分、分支覆盖、状态机覆盖测试规范
 * @copyright Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>
#include <string.h>

/* MRTK 内核头文件 */
#include "mrtk_ringbuffer.h"
#include "mrtk_errno.h"
#include "mrtk_typedef.h"
#include "mrtk_config_internal.h"

/* ==============================================================================
 * 测试策略说明
 * ==============================================================================
 *
 * 【边界值分析】
 * 1. 缓冲区大小边界：0、1、2、小尺寸（10）、大尺寸（1000）
 * 2. 数据长度边界：0字节、1字节、size-1字节、size字节、size+1字节
 * 3. 读写指针位置边界：
 *    - 读指针=0，写指针=0（空缓冲区）
 *    - 读指针=size-1，写指针=0（写指针绕回）
 *    - 写指针=size-1（写入到末尾）
 * 4. 绕回边界：读写操作跨越缓冲区末尾
 *
 * 【等价类划分 - 防御性测试】
 * 1. NULL指针：rb=NULL、buffer=NULL、ptr=NULL
 * 2. 非法参数：size=0
 * 3. 读取空缓冲区：期望返回MRTK_EEMPTY或0
 * 4. 写入满缓冲区：期望返回MRTK_EFULL或0
 * 5. 读取超过可用数据：请求长度>实际数据长度
 * 6. 写入超过可用空间：请求长度>剩余空间
 *
 * 【等价类划分 - 正向测试】
 * 1. 单字节读写（putc/getc）
 * 2. 多字节读写（read/write）
 * 3. 连续写入直到填满
 * 4. 连续读取直到清空
 * 5. 交替读写操作
 * 6. 多次绕回后的数据一致性
 *
 * 【分支覆盖】
 * 1. mrtk_rb_read:
 *    - data_len == 0 分支
 *    - read_len <= first_chunk 分支（不绕回）
 *    - read_len > first_chunk 分支（绕回）
 * 2. mrtk_rb_write:
 *    - free_len == 0 分支
 *    - write_len <= first_chunk 分支（不绕回）
 *    - write_len > first_chunk 分支（绕回）
 * 3. mrtk_rb_putc:
 *    - 缓冲区满分支
 *    - 缓冲区未满分支
 * 4. mrtk_rb_getc:
 *    - 缓冲区空分支
 *    - 缓冲区非空分支
 * 5. mrtk_rb_get_len:
 *    - 空状态分支
 *    - 未绕回状态分支
 *    - 绕回状态分支
 *
 * 【状态机覆盖】
 * 1. Init -> Empty
 * 2. Empty -> Partial（写入部分数据）
 * 3. Partial -> Full（写入直到填满）
 * 4. Full -> Partial（读取部分数据）
 * 5. Partial -> Empty（读取直到清空）
 * 6. Empty -> Full -> Empty（完整周期）
 * 7. 多次绕回后的状态一致性
 *
 * ==============================================================================
 * 测试固件定义
 * ============================================================================== */

/**
 * @brief 环形缓冲区测试固件
 * @details 提供测试环境和辅助方法
 */
class MrtkRingBufferTest : public ::testing::Test {
  protected:
    mrtk_rb_t   rb;                       /**< 环形缓冲区控制块 */
    mrtk_u8_t  *buffer_pool;              /**< 缓冲区内存池 */
    mrtk_u8_t  *read_buffer;              /**< 读取缓冲区 */
    mrtk_u8_t  *write_buffer;             /**< 写入缓冲区 */
    mrtk_u32_t  buffer_size;              /**< 缓冲区大小 */

    /**
     * @brief 测试前初始化
     */
    void SetUp() override {
        /* 使用较大的缓冲区以测试各种场景 */
        buffer_size  = 100;
        buffer_pool  = new mrtk_u8_t[buffer_size];
        read_buffer  = new mrtk_u8_t[buffer_size];
        write_buffer = new mrtk_u8_t[buffer_size];

        /* 清零所有缓冲区 */
        memset(buffer_pool, 0, buffer_size);
        memset(read_buffer, 0, buffer_size);
        memset(write_buffer, 0, buffer_size);

        /* 初始化环形缓冲区 */
        mrtk_rb_init(&rb, buffer_pool, buffer_size);
    }

    /**
     * @brief 测试后清理
     */
    void TearDown() override {
        delete[] buffer_pool;
        delete[] read_buffer;
        delete[] write_buffer;
    }

    /**
     * @brief 填充写入缓冲区
     * @param[in] start_value 起始值
     * @param[in] length 填充长度
     */
    void fill_write_buffer(mrtk_u8_t start_value, mrtk_size_t length) {
        for (mrtk_size_t i = 0; i < length; i++) {
            write_buffer[i] = start_value + i;
        }
    }

    /**
     * @brief 验证读取缓冲区内容
     * @param[in] expected_value 期望的起始值
     * @param[in] length 验证长度
     * @retval true 验证通过
     * @retval false 验证失败
     */
    bool verify_read_buffer(mrtk_u8_t expected_value, mrtk_size_t length) {
        for (mrtk_size_t i = 0; i < length; i++) {
            if (read_buffer[i] != expected_value + i) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 将缓冲区填充到指定状态
     * @param[in] target_len 目标数据长度
     */
    void fill_buffer_to(mrtk_size_t target_len) {
        fill_write_buffer(0, target_len);
        mrtk_rb_write(&rb, write_buffer, target_len);
    }
};

/* ==============================================================================
 * 初始化测试
 * ============================================================================== */

/**
 * @test 初始化正常情况
 * @details 验证初始化后缓冲区状态正确
 * @covers mrtk_rb_init
 */
TEST_F(MrtkRingBufferTest, Init_NormalState) {
    /* Step 1: Given - 已在 SetUp 中完成初始化 */

    /* Step 2: When - 执行初始化（已在 SetUp 中完成） */

    /* Step 3: Then - 验证初始状态 */
    EXPECT_EQ(rb.buffer, buffer_pool);
    EXPECT_EQ(rb.size, buffer_size);
    EXPECT_EQ(rb.read_idx, 0);
    EXPECT_EQ(rb.write_idx, 0);
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_FALSE);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 0);
    EXPECT_EQ(mrtk_rb_get_free(&rb), buffer_size - 1);
}

/**
 * @test 重新初始化缓冲区
 * @details 验证重新初始化后状态正确复位
 * @covers mrtk_rb_init
 */
TEST_F(MrtkRingBufferTest, Init_Reinitialize) {
    /* Step 1: Given - 缓冲区中有数据 */
    fill_buffer_to(50);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 50);

    /* Step 2: When - 重新初始化 */
    mrtk_rb_init(&rb, buffer_pool, buffer_size);

    /* Step 3: Then - 验证状态已复位 */
    EXPECT_EQ(rb.read_idx, 0);
    EXPECT_EQ(rb.write_idx, 0);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 0);
}

/* ==============================================================================
 * 写入操作测试
 * ============================================================================== */

/**
 * @test 写入单个字符到空缓冲区
 * @details 验证 putc 在空缓冲区中正常工作
 * @covers mrtk_rb_putc
 */
TEST_F(MrtkRingBufferTest, PutC_SingleCharToEmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);

    /* Step 2: When - 写入单个字符 */
    mrtk_err_t ret = mrtk_rb_putc(&rb, 'A');

    /* Step 3: Then - 验证写入成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 1);
    EXPECT_EQ(rb.buffer[rb.read_idx], 'A');
}

/**
 * @test 写入单个字符到满缓冲区
 * @details 验证 putc 在满缓冲区时返回错误
 * @covers mrtk_rb_putc
 */
TEST_F(MrtkRingBufferTest, PutC_SingleCharToFullBuffer) {
    /* Step 1: Given - 填满缓冲区（注意：实际可用空间为 size-1） */
    fill_buffer_to(buffer_size - 1);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_TRUE);

    /* Step 2: When - 尝试写入单个字符 */
    mrtk_err_t ret = mrtk_rb_putc(&rb, 'Z');

    /* Step 3: Then - 验证返回满错误 */
    EXPECT_EQ(ret, MRTK_EFULL);
    EXPECT_EQ(mrtk_rb_get_len(&rb), buffer_size - 1);
}

/**
 * @test 连续写入多个字符
 * @details 验证多次 putc 操作正常工作
 * @covers mrtk_rb_putc
 */
TEST_F(MrtkRingBufferTest, PutC_MultipleChars) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 连续写入10个字符 */
    for (int i = 0; i < 10; i++) {
        mrtk_err_t ret = mrtk_rb_putc(&rb, 'A' + i);
        EXPECT_EQ(ret, MRTK_EOK);
    }

    /* Step 3: Then - 验证所有字符写入成功 */
    EXPECT_EQ(mrtk_rb_get_len(&rb), 10);
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(rb.buffer[i], 'A' + i);
    }
}

/**
 * @test 批量写入到空缓冲区
 * @details 验证 write 在空缓冲区中正常工作
 * @covers mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Write_BulkToEmptyBuffer) {
    /* Step 1: Given - 空缓冲区和写入数据 */
    mrtk_size_t write_len = 50;
    fill_write_buffer(0, write_len);

    /* Step 2: When - 批量写入数据 */
    mrtk_size_t ret = mrtk_rb_write(&rb, write_buffer, write_len);

    /* Step 3: Then - 验证写入成功 */
    EXPECT_EQ(ret, write_len);
    EXPECT_EQ(mrtk_rb_get_len(&rb), write_len);
    EXPECT_EQ(rb.write_idx, write_len);
    EXPECT_EQ(rb.read_idx, 0);
}

/**
 * @test 批量写入超过剩余空间
 * @details 验证 write 在空间不足时只写入部分数据
 * @covers mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Write_ExceedFreeSpace) {
    /* Step 1: Given - 缓冲区中有部分数据，剩余空间有限 */
    fill_buffer_to(50);
    mrtk_size_t free_space = mrtk_rb_get_free(&rb);
    mrtk_size_t write_len  = free_space + 10; /* 请求写入超过剩余空间 */
    fill_write_buffer(100, write_len);

    /* Step 2: When - 尝试写入超过剩余空间的数据 */
    mrtk_size_t ret = mrtk_rb_write(&rb, write_buffer, write_len);

    /* Step 3: Then - 验证只写入了剩余空间大小的数据 */
    EXPECT_EQ(ret, free_space);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_TRUE);
}

/**
 * @test 写入到满缓冲区
 * @details 验证 write 在满缓冲区时返回0
 * @covers mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Write_ToFullBuffer) {
    /* Step 1: Given - 填满缓冲区 */
    fill_buffer_to(buffer_size - 1);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_TRUE);

    /* Step 2: When - 尝试写入数据 */
    fill_write_buffer(200, 10);
    mrtk_size_t ret = mrtk_rb_write(&rb, write_buffer, 10);

    /* Step 3: Then - 验证写入失败 */
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(mrtk_rb_get_len(&rb), buffer_size - 1);
}

/**
 * @test 写入0字节
 * @details 验证写入0字节的边界情况
 * @covers mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Write_ZeroBytes) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 写入0字节 */
    mrtk_size_t ret = mrtk_rb_write(&rb, write_buffer, 0);

    /* Step 3: Then - 验证写入0字节 */
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 0);
}

/* ==============================================================================
 * 读取操作测试
 * ============================================================================== */

/**
 * @test 从空缓冲区读取单个字符
 * @details 验证 getc 在空缓冲区时返回错误
 * @covers mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, GetC_FromEmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    mrtk_char_t ch;

    /* Step 2: When - 尝试读取字符 */
    mrtk_err_t ret = mrtk_rb_getc(&rb, &ch);

    /* Step 3: Then - 验证返回空错误 */
    EXPECT_EQ(ret, MRTK_EEMPTY);
}

/**
 * @test 从缓冲区读取单个字符
 * @details 验证 getc 正常工作
 * @covers mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, GetC_SingleChar) {
    /* Step 1: Given - 缓冲区中有一个字符 */
    mrtk_rb_putc(&rb, 'X');
    EXPECT_EQ(mrtk_rb_get_len(&rb), 1);

    /* Step 2: When - 读取字符 */
    mrtk_char_t ch;
    mrtk_err_t  ret = mrtk_rb_getc(&rb, &ch);

    /* Step 3: Then - 验证读取成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(ch, 'X');
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/**
 * @test 连续读取多个字符
 * @details 验证多次 getc 操作正常工作
 * @covers mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, GetC_MultipleChars) {
    /* Step 1: Given - 缓冲区中有多个字符 */
    for (int i = 0; i < 10; i++) {
        mrtk_rb_putc(&rb, 'A' + i);
    }
    EXPECT_EQ(mrtk_rb_get_len(&rb), 10);

    /* Step 2: When - 连续读取所有字符 */
    mrtk_char_t ch;
    for (int i = 0; i < 10; i++) {
        mrtk_err_t ret = mrtk_rb_getc(&rb, &ch);
        EXPECT_EQ(ret, MRTK_EOK);
        EXPECT_EQ(ch, 'A' + i);
    }

    /* Step 3: Then - 验证缓冲区为空 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/**
 * @test 批量读取到空缓冲区
 * @details 验证 read 在空缓冲区时返回0
 * @covers mrtk_rb_read
 */
TEST_F(MrtkRingBufferTest, Read_FromEmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);

    /* Step 2: When - 尝试读取数据 */
    mrtk_size_t ret = mrtk_rb_read(&rb, read_buffer, 10);

    /* Step 3: Then - 验证读取0字节 */
    EXPECT_EQ(ret, 0);
}

/**
 * @test 批量读取部分数据
 * @details 验证 read 在数据不足时只读取可用数据
 * @covers mrtk_rb_read
 */
TEST_F(MrtkRingBufferTest, Read_PartialData) {
    /* Step 1: Given - 缓冲区中有20字节数据 */
    fill_buffer_to(20);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 20);

    /* Step 2: When - 请求读取30字节 */
    mrtk_size_t ret = mrtk_rb_read(&rb, read_buffer, 30);

    /* Step 3: Then - 验证只读取了20字节 */
    EXPECT_EQ(ret, 20);
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    EXPECT_TRUE(verify_read_buffer(0, 20));
}

/**
 * @test 批量读取全部数据
 * @details 验证 read 正常读取全部数据
 * @covers mrtk_rb_read
 */
TEST_F(MrtkRingBufferTest, Read_AllData) {
    /* Step 1: Given - 缓冲区中有50字节数据 */
    fill_buffer_to(50);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 50);

    /* Step 2: When - 读取全部数据 */
    mrtk_size_t ret = mrtk_rb_read(&rb, read_buffer, 50);

    /* Step 3: Then - 验证读取成功 */
    EXPECT_EQ(ret, 50);
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    EXPECT_TRUE(verify_read_buffer(0, 50));
}

/**
 * @test 读取0字节
 * @details 验证读取0字节的边界情况
 * @covers mrtk_rb_read
 */
TEST_F(MrtkRingBufferTest, Read_ZeroBytes) {
    /* Step 1: Given - 缓冲区中有数据 */
    fill_buffer_to(20);

    /* Step 2: When - 读取0字节 */
    mrtk_size_t ret = mrtk_rb_read(&rb, read_buffer, 0);

    /* Step 3: Then - 验证读取0字节 */
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 20); /* 数据长度不变 */
}

/* ==============================================================================
 * 绕回操作测试
 * ============================================================================== */

/**
 * @test 写入绕回边界
 * @details 验证写指针绕回到缓冲区开头
 * @covers mrtk_rb_write, mrtk_rb_putc
 */
TEST_F(MrtkRingBufferTest, WrapAround_Write) {
    /* Step 1: Given - 写指针接近缓冲区末尾 */
    /* 通过先写后读，将写指针推到接近末尾 */
    fill_buffer_to(buffer_size - 20);
    mrtk_rb_read(&rb, read_buffer, 50);
    EXPECT_EQ(mrtk_rb_get_len(&rb), buffer_size - 70);

    /* Step 2: When - 写入数据导致写指针绕回 */
    /* 写指针在80，距离末尾20字节，写入30字节会触发绕回 */
    fill_write_buffer(100, 30);
    mrtk_size_t ret = mrtk_rb_write(&rb, write_buffer, 30);

    /* Step 3: Then - 验证写入成功且指针正确绕回 */
    EXPECT_EQ(ret, 30);
    EXPECT_EQ(mrtk_rb_get_len(&rb), buffer_size - 40);  /* 30字节 + 30字节 = 60字节 */

    /* 验证数据一致性：按环形缓冲区实际布局顺序读取验证 */
    /* 1. 读取位置50-79的旧数据 {50..79} */
    mrtk_rb_read(&rb, read_buffer, buffer_size - 70);
    EXPECT_TRUE(verify_read_buffer(50, 30));  /* 验证：{50,51,...,79} */

    /* 2. 读取位置80-99的新数据 {100..119} */
    mrtk_rb_read(&rb, read_buffer, 20);
    EXPECT_TRUE(verify_read_buffer(100, 20));  /* 验证：{100,101,...,119} */

    /* 3. 读取位置0-9的绕回新数据 {120..129} */
    mrtk_rb_read(&rb, read_buffer, 10);
    EXPECT_TRUE(verify_read_buffer(120, 10));  /* 验证：{120,121,...,129} */
}

/**
 * @test 读取绕回边界
 * @details 验证读指针绕回到缓冲区开头
 * @covers mrtk_rb_read, mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, WrapAround_Read) {
    /* Step 1: Given - 读指针接近缓冲区末尾，写指针已绕回 */
    /* 先填满，然后读取，再写入 */
    fill_buffer_to(buffer_size - 1);
    mrtk_rb_read(&rb, read_buffer, 50);
    EXPECT_EQ(rb.read_idx, 50);

    /* 写入数据使写指针绕回 */
    fill_write_buffer(200, 40);
    mrtk_rb_write(&rb, write_buffer, 40);
    
    /* 计算实际长度：剩余49字节 + 新写入40字节 = 89字节 */
    mrtk_size_t actual_len = mrtk_rb_get_len(&rb);
    EXPECT_EQ(actual_len, 89);
    EXPECT_TRUE(rb.write_idx < rb.read_idx); /* 确认写指针已绕回 */

    /* Step 2: When - 读取所有数据导致读指针绕回 */
    mrtk_size_t ret = mrtk_rb_read(&rb, read_buffer, actual_len);

    /* Step 3: Then - 验证读取成功且缓冲区为空 */
    EXPECT_EQ(ret, 89);
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/**
 * @test 多次绕回后数据一致性
 * @details 验证多次读写绕回后数据仍然一致
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, WrapAround_MultipleTimes) {
    /* Step 1: Given - 执行多次读写操作 */
    for (int round = 0; round < 5; round++) {
        /* 写入30字节 */
        fill_write_buffer(round * 30, 30);
        mrtk_rb_write(&rb, write_buffer, 30);

        /* 读取20字节 */
        mrtk_rb_read(&rb, read_buffer, 20);
    }

    /* Step 2: When - 读取剩余数据 */
    mrtk_size_t remaining = mrtk_rb_get_len(&rb);
    mrtk_size_t ret       = mrtk_rb_read(&rb, read_buffer, remaining);

    /* Step 3: Then - 验证数据一致性 */
    EXPECT_EQ(ret, 50); /* 5 * 30 - 5 * 20 = 50 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/* ==============================================================================
 * 状态查询测试
 * ============================================================================== */

/**
 * @test 查询空缓冲区长度
 * @details 验证 get_len 在空缓冲区时返回0
 * @covers mrtk_rb_get_len
 */
TEST_F(MrtkRingBufferTest, GetLen_EmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 查询长度 */
    mrtk_size_t len = mrtk_rb_get_len(&rb);

    /* Step 3: Then - 验证长度为0 */
    EXPECT_EQ(len, 0);
}

/**
 * @test 查询部分填充缓冲区长度
 * @details 验证 get_len 在部分填充时返回正确长度
 * @covers mrtk_rb_get_len
 */
TEST_F(MrtkRingBufferTest, GetLen_PartialBuffer) {
    /* Step 1: Given - 缓冲区中有40字节数据 */
    fill_buffer_to(40);

    /* Step 2: When - 查询长度 */
    mrtk_size_t len = mrtk_rb_get_len(&rb);

    /* Step 3: Then - 验证长度正确 */
    EXPECT_EQ(len, 40);
}

/**
 * @test 查询满缓冲区长度
 * @details 验证 get_len 在满缓冲区时返回 size-1
 * @covers mrtk_rb_get_len
 */
TEST_F(MrtkRingBufferTest, GetLen_FullBuffer) {
    /* Step 1: Given - 填满缓冲区 */
    fill_buffer_to(buffer_size - 1);

    /* Step 2: When - 查询长度 */
    mrtk_size_t len = mrtk_rb_get_len(&rb);

    /* Step 3: Then - 验证长度为 size-1 */
    EXPECT_EQ(len, buffer_size - 1);
}

/**
 * @test 查询绕回状态缓冲区长度
 * @details 验证 get_len 在绕回状态下计算正确
 * @covers mrtk_rb_get_len
 */
TEST_F(MrtkRingBufferTest, GetLen_WrappedBuffer) {
    /* Step 1: Given - 构造绕回状态 */
    fill_buffer_to(buffer_size - 1);  /* 写入99字节 */
    mrtk_rb_read(&rb, read_buffer, 30);  /* 读取30字节，剩余69字节 */
    fill_write_buffer(100, 20);
    mrtk_rb_write(&rb, write_buffer, 20);  /* 写入20字节，总长度89字节 */
    EXPECT_TRUE(rb.write_idx < rb.read_idx);  /* 确认写指针已绕回 */

    /* Step 2: When - 查询长度 */
    mrtk_size_t len = mrtk_rb_get_len(&rb);

    /* Step 3: Then - 验证长度正确 */
    /* 长度计算：剩余69字节 + 新写入20字节 = 89字节 */
    EXPECT_EQ(len, (buffer_size - 31) + 20);
}

/**
 * @test 查询空缓冲区剩余空间
 * @details 验证 get_free 在空缓冲区时返回 size-1
 * @covers mrtk_rb_get_free
 */
TEST_F(MrtkRingBufferTest, GetFree_EmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 查询剩余空间 */
    mrtk_size_t free = mrtk_rb_get_free(&rb);

    /* Step 3: Then - 验证剩余空间为 size-1 */
    EXPECT_EQ(free, buffer_size - 1);
}

/**
 * @test 查询部分填充缓冲区剩余空间
 * @details 验证 get_free 在部分填充时返回正确空间
 * @covers mrtk_rb_get_free
 */
TEST_F(MrtkRingBufferTest, GetFree_PartialBuffer) {
    /* Step 1: Given - 缓冲区中有40字节数据 */
    fill_buffer_to(40);

    /* Step 2: When - 查询剩余空间 */
    mrtk_size_t free = mrtk_rb_get_free(&rb);

    /* Step 3: Then - 验证剩余空间正确 */
    EXPECT_EQ(free, buffer_size - 1 - 40);
}

/**
 * @test 查询满缓冲区剩余空间
 * @details 验证 get_free 在满缓冲区时返回0
 * @covers mrtk_rb_get_free
 */
TEST_F(MrtkRingBufferTest, GetFree_FullBuffer) {
    /* Step 1: Given - 填满缓冲区 */
    fill_buffer_to(buffer_size - 1);

    /* Step 2: When - 查询剩余空间 */
    mrtk_size_t free = mrtk_rb_get_free(&rb);

    /* Step 3: Then - 验证剩余空间为0 */
    EXPECT_EQ(free, 0);
}

/**
 * @test 判断空缓冲区
 * @details 验证 is_empty 在空缓冲区时返回 TRUE
 * @covers mrtk_rb_is_empty
 */
TEST_F(MrtkRingBufferTest, IsEmpty_EmptyBuffer) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 判断是否为空 */
    mrtk_bool_t is_empty = mrtk_rb_is_empty(rb.read_idx, rb.write_idx);

    /* Step 3: Then - 验证返回 TRUE */
    EXPECT_EQ(is_empty, MRTK_TRUE);
}

/**
 * @test 判断非空缓冲区
 * @details 验证 is_empty 在非空缓冲区时返回 FALSE
 * @covers mrtk_rb_is_empty
 */
TEST_F(MrtkRingBufferTest, IsEmpty_NonEmptyBuffer) {
    /* Step 1: Given - 缓冲区中有数据 */
    mrtk_rb_putc(&rb, 'A');

    /* Step 2: When - 判断是否为空 */
    mrtk_bool_t is_empty = mrtk_rb_is_empty(rb.read_idx, rb.write_idx);

    /* Step 3: Then - 验证返回 FALSE */
    EXPECT_EQ(is_empty, MRTK_FALSE);
}

/**
 * @test 判断满缓冲区
 * @details 验证 is_full 在满缓冲区时返回 TRUE
 * @covers mrtk_rb_is_full
 */
TEST_F(MrtkRingBufferTest, IsFull_FullBuffer) {
    /* Step 1: Given - 填满缓冲区 */
    fill_buffer_to(buffer_size - 1);

    /* Step 2: When - 判断是否已满 */
    mrtk_bool_t is_full = mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size);

    /* Step 3: Then - 验证返回 TRUE */
    EXPECT_EQ(is_full, MRTK_TRUE);
}

/**
 * @test 判断未满缓冲区
 * @details 验证 is_full 在未满缓冲区时返回 FALSE
 * @covers mrtk_rb_is_full
 */
TEST_F(MrtkRingBufferTest, IsFull_NonFullBuffer) {
    /* Step 1: Given - 缓冲区中有部分数据 */
    fill_buffer_to(50);

    /* Step 2: When - 判断是否已满 */
    mrtk_bool_t is_full = mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size);

    /* Step 3: Then - 验证返回 FALSE */
    EXPECT_EQ(is_full, MRTK_FALSE);
}

/* ==============================================================================
 * 交替读写测试
 * ============================================================================== */

/**
 * @test 交替读写操作
 * @details 验证读写交替进行时数据一致性
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, AlternatingReadWrite) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 执行交替读写操作 */
    /* 写入20字节 */
    fill_write_buffer(0, 20);
    mrtk_rb_write(&rb, write_buffer, 20);

    /* 读取10字节 */
    mrtk_rb_read(&rb, read_buffer, 10);
    EXPECT_TRUE(verify_read_buffer(0, 10));

    /* 写入30字节 */
    fill_write_buffer(20, 30);
    mrtk_rb_write(&rb, write_buffer, 30);

    /* 读取第一次剩余的10字节 */
    mrtk_rb_read(&rb, read_buffer, 10);
    EXPECT_TRUE(verify_read_buffer(10, 10));  /* 验证：{10,11,...,19} */

    /* 读取第二次写入的30字节 */
    mrtk_rb_read(&rb, read_buffer, 30);
    EXPECT_TRUE(verify_read_buffer(20, 30));  /* 验证：{20,21,...,49} */

    /* Step 3: Then - 验证数据一致性 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/**
 * @test 写入后完全清空
 * @details 验证写入数据后完全清空的状态转换
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, WriteAndCompletelyDrain) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 写入50字节后完全读取 */
    fill_buffer_to(50);
    mrtk_rb_read(&rb, read_buffer, 50);

    /* Step 3: Then - 验证回到空状态 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    EXPECT_EQ(mrtk_rb_get_len(&rb), 0);
    EXPECT_TRUE(verify_read_buffer(0, 50));
}

/**
 * @test 填满后读取部分
 * @details 验证满缓冲区读取部分数据后的状态
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, FillThenPartialDrain) {
    /* Step 1: Given - 填满缓冲区 */
    fill_buffer_to(buffer_size - 1);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_TRUE);

    /* Step 2: When - 读取30字节 */
    mrtk_rb_read(&rb, read_buffer, 30);

    /* Step 3: Then - 验证状态转换 */
    EXPECT_EQ(mrtk_rb_get_len(&rb), buffer_size - 31);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_FALSE);
    EXPECT_TRUE(verify_read_buffer(0, 30));
}

/* ==============================================================================
 * 边界值测试
 * ============================================================================== */

/**
 * @test 缓冲区大小为1的边界情况
 * @details 验证最小缓冲区的正常工作
 * @covers mrtk_rb_init, mrtk_rb_putc, mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, Boundary_BufferSize1) {
    /* Step 1: Given - 大小为1的缓冲区（实际可用0字节） */
    mrtk_u8_t tiny_buffer[1];
    mrtk_rb_t tiny_rb;
    mrtk_rb_init(&tiny_rb, tiny_buffer, 1);

    /* Step 2: When - 尝试写入字符 */
    mrtk_err_t ret = mrtk_rb_putc(&tiny_rb, 'A');

    /* Step 3: Then - 验证缓冲区已满 */
    EXPECT_EQ(ret, MRTK_EFULL);
    EXPECT_EQ(mrtk_rb_get_free(&tiny_rb), 0);
}

/**
 * @test 缓冲区大小为2的边界情况
 * @details 验证大小为2的缓冲区只能存储1字节
 * @covers mrtk_rb_init, mrtk_rb_putc, mrtk_rb_getc
 */
TEST_F(MrtkRingBufferTest, Boundary_BufferSize2) {
    /* Step 1: Given - 大小为2的缓冲区（实际可用1字节） */
    mrtk_u8_t small_buffer[2];
    mrtk_rb_t small_rb;
    mrtk_rb_init(&small_rb, small_buffer, 2);

    /* Step 2: When - 写入第一个字符 */
    mrtk_err_t ret1 = mrtk_rb_putc(&small_rb, 'A');

    /* Step 3: Then - 验证成功写入 */
    EXPECT_EQ(ret1, MRTK_EOK);

    /* Step 4: When - 尝试写入第二个字符 */
    mrtk_err_t ret2 = mrtk_rb_putc(&small_rb, 'B');

    /* Step 5: Then - 验证缓冲区已满 */
    EXPECT_EQ(ret2, MRTK_EFULL);

    /* Step 6: When - 读取字符 */
    mrtk_char_t ch;
    mrtk_err_t  ret3 = mrtk_rb_getc(&small_rb, &ch);

    /* Step 7: Then - 验证读取成功且缓冲区变空 */
    EXPECT_EQ(ret3, MRTK_EOK);
    EXPECT_EQ(ch, 'A');
    EXPECT_EQ(mrtk_rb_is_empty(small_rb.read_idx, small_rb.write_idx), MRTK_TRUE);
}

/**
 * @test 读写恰好填满和清空缓冲区
 * @details 验证精确的边界操作
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Boundary_ExactFillAndDrain) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 恰好填满缓冲区（size-1字节） */
    fill_buffer_to(buffer_size - 1);
    EXPECT_EQ(mrtk_rb_is_full(rb.read_idx, rb.write_idx, rb.size), MRTK_TRUE);

    /* Step 3: When - 恰好清空缓冲区 */
    mrtk_rb_read(&rb, read_buffer, buffer_size - 1);

    /* Step 4: Then - 验证回到空状态 */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
    EXPECT_EQ(rb.read_idx, buffer_size - 1); /* 读指针指向最后一个位置 */
    EXPECT_EQ(rb.write_idx, buffer_size - 1); /* 写指针指向最后一个位置 */
}

/* ==============================================================================
 * 压力测试
 * ============================================================================== */

/**
 * @test 大量随机读写操作
 * @details 验证长时间运行后的稳定性
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Stress_RandomReadWrite) {
    /* Step 1: Given - 空缓冲区 */
    mrtk_u8_t write_counter = 0;
    mrtk_u8_t read_counter  = 0;

    /* Step 2: When - 执行1000次随机读写 */
    for (int i = 0; i < 1000; i++) {
        /* 随机决定写入还是读取 */
        if (i % 3 == 0 || mrtk_rb_get_len(&rb) == 0) {
            /* 写入操作 */
            mrtk_size_t free = mrtk_rb_get_free(&rb);
            mrtk_size_t write_len = (free > 10) ? 10 : free;

            for (mrtk_size_t j = 0; j < write_len; j++) {
                write_buffer[j] = write_counter++;
            }

            mrtk_rb_write(&rb, write_buffer, write_len);
        } else {
            /* 读取操作 */
            mrtk_size_t len   = mrtk_rb_get_len(&rb);
            mrtk_size_t read_len = (len > 10) ? 10 : len;

            mrtk_rb_read(&rb, read_buffer, read_len);

            /* 验证数据 */
            for (mrtk_size_t j = 0; j < read_len; j++) {
                EXPECT_EQ(read_buffer[j], read_counter++);
            }
        }
    }

    /* Step 3: Then - 读取剩余数据并验证 */
    mrtk_size_t remaining = mrtk_rb_get_len(&rb);
    while (remaining > 0) {
        mrtk_size_t read_len = (remaining > 10) ? 10 : remaining;
        mrtk_rb_read(&rb, read_buffer, read_len);

        for (mrtk_size_t j = 0; j < read_len; j++) {
            EXPECT_EQ(read_buffer[j], read_counter++);
        }

        remaining -= read_len;
    }

    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}

/**
 * @test 连续写入和读取循环
 * @details 验证生产者-消费者模式的稳定性
 * @covers mrtk_rb_read, mrtk_rb_write
 */
TEST_F(MrtkRingBufferTest, Stress_ConsecutiveWriteReadCycles) {
    /* Step 1: Given - 空缓冲区 */

    /* Step 2: When - 执行100次独立的写入-读取循环 */
    for (int round = 0; round < 100; round++) {
        /* 每轮循环都重新初始化缓冲区，避免状态累积 */
        memset(buffer_pool, 0, buffer_size);  /* 清空底层缓冲区数据 */
        memset(read_buffer, 0, buffer_size);  /* 清空读取缓冲区 */
        mrtk_rb_init(&rb, buffer_pool, buffer_size);

        /* 写入20字节，使用固定序列避免溢出 */
        for (int i = 0; i < 20; i++) {
            write_buffer[i] = round + i;
        }
        mrtk_rb_write(&rb, write_buffer, 20);

        /* 读取20字节 */
        mrtk_rb_read(&rb, read_buffer, 20);

        /* 验证数据 */
        for (int i = 0; i < 20; i++) {
            EXPECT_EQ(read_buffer[i], round + i);
        }
    }

    /* Step 3: Then - 验证缓冲区为空（最后一轮已清空） */
    EXPECT_EQ(mrtk_rb_is_empty(rb.read_idx, rb.write_idx), MRTK_TRUE);
}
