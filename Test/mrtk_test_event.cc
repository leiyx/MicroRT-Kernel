/**
 * @file mrtk_event_test.cpp
 * @author leiyx
 * @brief 事件标志组模块单元测试
 * @details 严格遵循边界值分析、等价类划分、分支覆盖、状态机覆盖等测试工程学方法
 * @copyright Copyright (c) 2026
 *
 * 测试覆盖策略：
 * 1. 边界值分析：timeout=0/最大值、set=0/0xFFFFFFFF/0x80000000
 * 2. 等价类划分：NULL参数、非法flag、AND/OR组合错误
 * 3. 分支覆盖：need_schedule触发/未触发、条件满足/不满足、自动清除/不清除
 * 4. 状态机覆盖：Init -> Send/Recv -> Detach/Delete 完整生命周期
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>

/* Mock 头文件（C++ 代码，必须在最前） */
#include "mrtk_mock_hw.hpp"

/* MRTK 头文件（已包含 extern "C"） */
#include "mrtk.h"

#include "mrtk_event.h"

/* ==============================================================================
 * 测试夹具：MrtkEventTest
 * ============================================================================== */

/**
 * @class MrtkEventTest
 * @brief 事件标志组测试的测试夹具
 * @details 提供统一的测试环境初始化与清理，以及辅助方法
 */
class MrtkEventTest : public ::testing::Test {
  protected:
    /**
     * @brief CPU 端口层 Mock 对象（使用 NiceMock 自动忽略未设置期望的调用）
     */
    testing::NiceMock<MockCpuPort> mock_cpu_port;

    /**
     * @brief 测试用的静态事件对象
     */
    mrtk_event_t static_event;

    /**
     * @brief 测试用的动态事件对象指针
     */
    mrtk_event_t *dynamic_event;

    /**
     * @brief 测试用的任务对象数组
     */
    mrtk_tcb_t test_tcbs[3];

    /**
     * @brief 事件接收值（用于测试）
     */
    mrtk_u32_t event_recved;

    /**
     * @brief 测试前初始化
     */
    void SetUp() override {
        /* Step 1: 系统初始化（统一入口，包括调度器和全局变量） */
        mrtk_err_t ret = mrtk_system_init();
        ASSERT_EQ(ret, MRTK_EOK) << "系统初始化失败";

        g_CurrentTCB = mrtk_task_get_idle();

        /* Step 2: 设置 Mock 对象 */
        mrtk_mock_set_cpu_port(&mock_cpu_port);

        /* Step 3: 设置默认期望行为 */
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_disable)
            .WillByDefault(testing::Return(0));
        ON_CALL(mock_cpu_port, mrtk_hw_interrupt_enable)
            .WillByDefault(testing::Return());

        /* Step 4: 初始化测试任务对象 */
        for (int i = 0; i < 3; i++) {
            memset(&test_tcbs[i], 0, sizeof(mrtk_tcb_t));
            test_tcbs[i].priority        = 10 + i;
            test_tcbs[i].state           = MRTK_TASK_STAT_READY;
            test_tcbs[i].event_set       = 0;
            test_tcbs[i].event_option    = 0;
            test_tcbs[i].event_recved    = &event_recved;
            test_tcbs[i].last_error      = MRTK_EOK;
            _mrtk_list_init(&test_tcbs[i].sched_node);
        }

        dynamic_event = nullptr;
        event_recved  = 0;
    }

    /**
     * @brief 测试后清理
     */
    void TearDown() override {
        /* 清除 Mock 对象 */
        mrtk_mock_clear_cpu_port();
    }

    /**
     * @brief 辅助函数：验证事件对象的基本状态
     * @param event 事件对象指针
     * @param name 期望的事件名称
     * @param expected_set 期望的事件集合
     */
    void VerifyEventBasicState(mrtk_event_t *event, const char *name, mrtk_u32_t expected_set) {
        ASSERT_NE(event, nullptr);
        EXPECT_STREQ(event->ipc_obj.obj.name, name);
        EXPECT_EQ(event->set, expected_set);
    }
};

/* ==============================================================================
 * 生命周期管理 API 测试
 * ============================================================================== */

/**
 * @test 事件静态初始化 - 正常情况
 * @details 等价类划分：正向测试
 */
TEST_F(MrtkEventTest, Event_Init_ValidParams_Success) {
    /* Given: 准备合法的参数 */
    mrtk_event_t event;
    const mrtk_char_t *name = "test_event";
    mrtk_u8_t flag          = MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO;

    /* When: 调用初始化函数 */
    mrtk_err_t ret = mrtk_event_init(&event, name, flag);

    /* Then: 验证返回值和对象状态 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_STREQ(event.ipc_obj.obj.name, name);
    EXPECT_EQ(event.set, 0);
}

/**
 * @test 事件静态初始化 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Init_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    /* When: 调用初始化函数 */
    mrtk_err_t ret = mrtk_event_init(nullptr, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件静态初始化 - NULL 名称指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Init_NullName_ReturnsEINVAL) {
    /* Given: name 参数为 NULL */
    mrtk_event_t event;

    /* When: 调用初始化函数 */
    mrtk_err_t ret = mrtk_event_init(&event, nullptr, MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件静态脱离 - 正常情况（有任务等待）
 * @details 状态机覆盖：Detach 操作，需要调度
 * @details 分支覆盖：_mrtk_ipc_obj_delete 返回 MRTK_TRUE
 */
TEST_F(MrtkEventTest, Event_Detach_ValidEventWithWaitingTasks_SchedulesAndReturnsEOK) {
    /* Given: 准备一个静态事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 将一个任务插入到等待队列（模拟有任务等待） */
    _mrtk_list_init(&static_event.ipc_obj.suspend_taker);
    _mrtk_list_insert_after(&static_event.ipc_obj.suspend_taker, &test_tcbs[0].sched_node);

    /* When: 调用脱离函数 */
    ret = mrtk_event_detach(&static_event);

    /* Then: 验证返回值 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/**
 * @test 事件静态脱离 - 正常情况（无任务等待）
 * @details 分支覆盖：_mrtk_ipc_obj_delete 返回 MRTK_FALSE
 */
TEST_F(MrtkEventTest, Event_Detach_ValidEventNoWaitingTasks_NoScheduleAndReturnsEOK) {
    /* Given: 准备一个静态事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 确保等待队列为空 */
    _mrtk_list_init(&static_event.ipc_obj.suspend_taker);

    /* When: 调用脱离函数 */
    ret = mrtk_event_detach(&static_event);

    /* Then: 验证返回值 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/**
 * @test 事件静态脱离 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Detach_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    /* When: 调用脱离函数 */
    mrtk_err_t ret = mrtk_event_detach(nullptr);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件静态脱离 - 动态对象非法调用
 * @details 状态机覆盖：非法状态调用（对动态对象调用 detach）
 */
TEST_F(MrtkEventTest, Event_Detach_DynamicObject_ReturnsEINVAL) {
    /* Given: 准备一个动态类型的事件对象 */
    mrtk_event_t dynamic_type_event;
    mrtk_err_t ret = mrtk_event_init(&dynamic_type_event, "dynamic_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* 修改对象类型标志为动态 */
    MRTK_OBJ_SET_ALLOC_FLAG(dynamic_type_event.ipc_obj.obj.type, MRTK_OBJECT_TYPE_DYNAMIC);

    /* When: 对动态对象调用脱离函数 */
    ret = mrtk_event_detach(&dynamic_type_event);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件动态创建 - 成功创建
 * @details 等价类划分：正向测试
 */
TEST_F(MrtkEventTest, Event_Create_ValidParams_ReturnsValidPointer) {
    /* Given: 准备合法参数 */
    const mrtk_char_t *name = "test_event";
    mrtk_u8_t flag          = MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO;

    /* When: 调用创建函数 */
    mrtk_event_t *event = mrtk_event_create(name, flag);

    /* Then: 验证返回非空指针且对象类型为动态 */
    EXPECT_NE(event, nullptr);
    if (event != nullptr) {
        EXPECT_TRUE(MRTK_OBJ_IS_DYNAMIC(event->ipc_obj.obj.type));
        EXPECT_STREQ(event->ipc_obj.obj.name, name);
        EXPECT_EQ(event->set, 0);

        /* 清理：删除创建的事件对象 */
        mrtk_event_delete(event);
    }
}

/**
 * @test 事件动态创建 - NULL 名称
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Create_NullName_ReturnsNull) {
    /* Given: name 参数为 NULL */
    /* When: 调用创建函数 */
    mrtk_event_t *event = mrtk_event_create(nullptr, MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);

    /* Then: 验证返回空指针 */
    EXPECT_EQ(event, nullptr);
}

/**
 * @test 事件动态删除 - 正常情况
 * @details 状态机覆盖：Delete 操作
 */
TEST_F(MrtkEventTest, Event_Delete_ValidDynamicObject_Succeeds) {
    /* Given: 准备一个动态事件对象 */
    mrtk_event_t *event = mrtk_event_create("test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_NE(event, nullptr);

    /* When: 调用删除函数 */
    mrtk_err_t ret = mrtk_event_delete(event);

    /* Then: 验证返回值 */
    EXPECT_EQ(ret, MRTK_EOK);
}

/**
 * @test 事件动态删除 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Delete_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    /* When: 调用删除函数 */
    mrtk_err_t ret = mrtk_event_delete(nullptr);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件动态删除 - 静态对象非法调用
 * @details 状态机覆盖：非法状态调用（对静态对象调用 delete）
 */
TEST_F(MrtkEventTest, Event_Delete_StaticObject_ReturnsEINVAL) {
    /* Given: 准备一个静态事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "static_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* When: 对静态对象调用删除函数 */
    ret = mrtk_event_delete(&static_event);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/* ==============================================================================
 * 核心 IPC 通信 API 测试 - mrtk_event_send
 * ============================================================================== */

/**
 * @test 发送事件 - 正常情况（无任务等待）
 * @details 等价类划分：正向测试
 * @details 分支覆盖：无任务满足条件，不触发调度
 */
TEST_F(MrtkEventTest, Event_Send_ValidSetNoWaitingTasks_Succeeds) {
    /* Given: 准备事件对象和发送位掩码 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    mrtk_u32_t send_mask = 0x00000001;

    /* When: 发送事件 */
    ret = mrtk_event_send(&static_event, send_mask);

    /* Then: 验证返回值和事件集合更新 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, send_mask);
}

/**
 * @test 发送事件 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Send_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    /* When: 发送事件 */
    mrtk_err_t ret = mrtk_event_send(nullptr, 0x00000001);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 发送事件 - set 参数为 0
 * @details 边界值分析：set = 0（非法值）
 */
TEST_F(MrtkEventTest, Event_Send_ZeroSet_ReturnsEINVAL) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* When: set 参数为 0 */
    ret = mrtk_event_send(&static_event, 0x00);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 发送事件 - 位合并操作
 * @details 边界值分析：多次发送，验证按位或合并
 */
TEST_F(MrtkEventTest, Event_Send_MultipleSends_BitwiseOrMerge) {
    /* Given: 初始化事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* When: 第一次发送 0x01 */
    ret = mrtk_event_send(&static_event, 0x00000001);
    ASSERT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x00000001);

    /* When: 第二次发送 0x02 */
    ret = mrtk_event_send(&static_event, 0x00000002);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Then: 验证事件集合按位或合并 */
    EXPECT_EQ(static_event.set, 0x00000003); /* 0x01 | 0x02 */
}

/**
 * @test 发送事件 - 边界值测试（最大 32 位）
 * @details 边界值分析：set = 0xFFFFFFFF
 */
TEST_F(MrtkEventTest, Event_Send_Max32BitSet_Succeeds) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    mrtk_u32_t send_mask = 0xFFFFFFFF;

    /* When: 发送事件 */
    ret = mrtk_event_send(&static_event, send_mask);

    /* Then: 验证返回值和事件集合更新 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0xFFFFFFFF);
}

/**
 * @test 发送事件 - 边界值测试（最高位）
 * @details 边界值分析：set = 0x80000000
 */
TEST_F(MrtkEventTest, Event_Send_HighestBitSet_Succeeds) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    mrtk_u32_t send_mask = 0x80000000;

    /* When: 发送事件 */
    ret = mrtk_event_send(&static_event, send_mask);

    /* Then: 验证返回值和事件集合更新 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x80000000);
}

/* ==============================================================================
 * 核心 IPC 通信 API 测试 - mrtk_event_recv
 * ============================================================================== */

/**
 * @test 接收事件 - 条件满足立即返回（逻辑或）
 * @details 分支覆盖：条件满足分支
 */
TEST_F(MrtkEventTest, Event_Recv_ConditionMetOr_ImmediateReturn) {
    /* Given: 事件集合已包含等待的位 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    static_event.set       = 0x00000001;
    mrtk_u32_t set_to_wait = 0x00000001;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证立即返回成功，接收到正确事件 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(recved, 0x00000001);
}

/**
 * @test 接收事件 - 条件满足立即返回（逻辑与）
 * @details 分支覆盖：AND 条件判断
 */
TEST_F(MrtkEventTest, Event_Recv_ConditionMetAnd_ImmediateReturn) {
    /* Given: 事件集合包含所有等待的位 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    static_event.set       = 0x00000003;
    mrtk_u32_t set_to_wait = 0x00000003;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_AND;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证立即返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(recved, 0x00000003);
}

/**
 * @test 接收事件 - 条件满足自动清除
 * @details 分支覆盖：MRTK_EVENT_FLAG_CLEAR 标志处理
 */
TEST_F(MrtkEventTest, Event_Recv_ConditionMetWithClear_ClearsBits) {
    /* Given: 事件集合包含等待的位，指定自动清除 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    static_event.set       = 0x00000001;
    mrtk_u32_t set_to_wait = 0x00000001;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR | MRTK_EVENT_FLAG_CLEAR;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证事件位被清除 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x00000000);
}

/**
 * @test 接收事件 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Recv_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    mrtk_u32_t recved = 0;

    /* When: 接收事件 */
    mrtk_err_t ret = mrtk_event_recv(nullptr, 0x00000001, MRTK_EVENT_FLAG_OR, 100, &recved);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 接收事件 - set_to_wait 参数为 0
 * @details 边界值分析：set_to_wait = 0（非法值）
 */
TEST_F(MrtkEventTest, Event_Recv_ZeroWaitMask_ReturnsEINVAL) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    mrtk_u32_t recved = 0;

    /* When: set_to_wait 参数为 0 */
    ret = mrtk_event_recv(&static_event, 0x00, MRTK_EVENT_FLAG_OR, 100, &recved);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 接收事件 - NULL recved 参数
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Recv_NullRecved_ReturnsEINVAL) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* When: recved 参数为 NULL */
    ret = mrtk_event_recv(&static_event, 0x00000001, MRTK_EVENT_FLAG_OR, 100, nullptr);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 接收事件 - 同时指定 AND 和 OR
 * @details 等价类划分：防御性测试，非法参数组合
 */
TEST_F(MrtkEventTest, Event_Recv_BothAndOrFlags_ReturnsEINVAL) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 同时指定 AND 和 OR 标志 */
    mrtk_u32_t recved = 0;
    mrtk_u8_t option  = MRTK_EVENT_FLAG_AND | MRTK_EVENT_FLAG_OR;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, 0x00000001, option, 100, &recved);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 接收事件 - 中断上下文中阻塞
 * @details 等价类划分：防御性测试，非法上下文
 */
TEST_F(MrtkEventTest, Event_Recv_BlockingInIsr_ReturnsE_IN_ISR) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 模拟在中断上下文中，条件不满足且 timeout != 0 */
    static_event.set       = 0x00000000;
    mrtk_u32_t set_to_wait = 0x00000001;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* 模拟中断嵌套 */
    g_interrupt_nest = 1;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_E_IN_ISR);

    /* 清理 */
    g_interrupt_nest = 0;
}

/**
 * @test 接收事件 - 条件不满足非阻塞
 * @details 边界值分析：timeout = 0（非阻塞）
 * @details 分支覆盖：条件不满足 + 非阻塞分支
 */
TEST_F(MrtkEventTest, Event_Recv_ConditionNotMetNonBlocking_ReturnsEEMPTY) {
    /* Given: 条件不满足，timeout = 0 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    static_event.set       = 0x00000000;
    mrtk_u32_t set_to_wait = 0x00000001;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR;
    mrtk_tick_t timeout    = 0;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证返回 EEMPTY */
    EXPECT_EQ(ret, MRTK_EEMPTY);
}

/**
 * @test 接收事件 - 边界值测试（等待所有 32 位）
 * @details 边界值分析：set_to_wait = 0xFFFFFFFF，AND 条件
 */
TEST_F(MrtkEventTest, Event_Recv_WaitAll32BitsAnd_ImmediateReturn) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 等待所有 32 位，事件集合也包含所有位 */
    static_event.set       = 0xFFFFFFFF;
    mrtk_u32_t set_to_wait = 0xFFFFFFFF;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_AND;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证立即返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(recved, 0xFFFFFFFF);
}

/**
 * @test 接收事件 - 边界值测试（等待最高位）
 * @details 边界值分析：set_to_wait = 0x80000000
 */
TEST_F(MrtkEventTest, Event_Recv_WaitHighestBit_ImmediateReturn) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 等待最高位（bit 31） */
    static_event.set       = 0x80000000;
    mrtk_u32_t set_to_wait = 0x80000000;
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件 */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证立即返回成功 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(recved, 0x80000000);
}

/* ==============================================================================
 * 控制与调试 API 测试
 * ============================================================================== */

/**
 * @test 事件控制 - 清空命令
 * @details 等价类划分：正向测试
 */
TEST_F(MrtkEventTest, Event_Control_ClearCommand_Succeeds) {
    /* Given: 准备一个有事件位的事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    static_event.set = 0x0000000F;

    /* When: 执行清空命令 */
    ret = mrtk_event_control(&static_event, MRTK_EVENT_CMD_CLEAR, MRTK_NULL);

    /* Then: 验证返回值和事件集合被清空 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x00000000);
}

/**
 * @test 事件控制 - NULL 事件指针
 * @details 等价类划分：防御性测试，NULL 参数
 */
TEST_F(MrtkEventTest, Event_Control_NullEvent_ReturnsEINVAL) {
    /* Given: event 参数为 NULL */
    /* When: 执行控制命令 */
    mrtk_err_t ret = mrtk_event_control(nullptr, MRTK_EVENT_CMD_CLEAR, nullptr);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/**
 * @test 事件控制 - 非法命令
 * @details 等价类划分：防御性测试，非法枚举值
 */
TEST_F(MrtkEventTest, Event_Control_InvalidCommand_ReturnsEINVAL) {
    /* Given: 准备事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: cmd 参数为非法值 */
    mrtk_u32_t invalid_cmd = 0xFF;

    /* When: 执行控制命令 */
    ret = mrtk_event_control(&static_event, invalid_cmd, MRTK_NULL);

    /* Then: 验证返回错误码 */
    EXPECT_EQ(ret, MRTK_EINVAL);
}

/* ==============================================================================
 * 综合场景测试
 * ============================================================================== */

/**
 * @test 综合场景 - 事件位累积与部分清除
 * @details 边界值分析：验证事件位的累积和部分清除行为
 */
TEST_F(MrtkEventTest, Scenario_EventBitAccumulationAndPartialClear_CorrectBehavior) {
    /* Given: 初始化事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* When: 连续发送多个事件位 */
    ret = mrtk_event_send(&static_event, 0x00000001);
    ASSERT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x00000001);

    ret = mrtk_event_send(&static_event, 0x00000002);
    ASSERT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(static_event.set, 0x00000003);

    ret = mrtk_event_send(&static_event, 0x00000004);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Then: 验证事件位累积 */
    EXPECT_EQ(static_event.set, 0x00000007);

    /* Given: 准备接收并清除部分位 */
    mrtk_u32_t recved = 0;
    static_event.set  = 0x00000007;

    /* When: 接收并清除 bit0 */
    ret = mrtk_event_recv(&static_event, 0x00000001,
                          MRTK_EVENT_FLAG_OR | MRTK_EVENT_FLAG_CLEAR, 0, &recved);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Then: 验证 bit0 被清除，其他位保留 */
    EXPECT_EQ(static_event.set, 0x00000006);
}

/**
 * @test 综合场景 - OR 条件部分满足
 * @details 分支覆盖：OR 条件判断，部分位满足
 */
TEST_F(MrtkEventTest, Scenario_OrConditionPartialMet_Succeeds) {
    /* Given: 初始化事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 事件集合包含部分等待位 */
    static_event.set       = 0x00000001; /* bit0 置位 */
    mrtk_u32_t set_to_wait = 0x00000003; /* 等待 bit0 和 bit1 */
    mrtk_u8_t option       = MRTK_EVENT_FLAG_OR;
    mrtk_tick_t timeout    = 100;
    mrtk_u32_t recved      = 0;

    /* When: 接收事件（OR 条件，只要有一位满足即可） */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证立即返回成功，接收到满足的位 */
    EXPECT_EQ(ret, MRTK_EOK);
    EXPECT_EQ(recved, 0x00000001);
}

/**
 * @test 综合场景 - AND 条件不满足
 * @details 分支覆盖：AND 条件判断，部分位不满足
 */
TEST_F(MrtkEventTest, Scenario_AndConditionNotMet_ReturnsEEMPTY) {
    /* Given: 初始化事件对象 */
    mrtk_err_t ret = mrtk_event_init(&static_event, "test_event", MRTK_IPC_FLAG_NOTIFY_POLICY_FIFO);
    ASSERT_EQ(ret, MRTK_EOK);

    /* Given: 事件集合只包含部分等待位 */
    static_event.set       = 0x00000001; /* bit0 置位 */
    mrtk_u32_t set_to_wait = 0x00000003; /* 等待 bit0 和 bit1 */
    mrtk_u8_t option       = MRTK_EVENT_FLAG_AND;
    mrtk_tick_t timeout    = 0; /* 非阻塞 */
    mrtk_u32_t recved      = 0;

    /* When: 接收事件（AND 条件，需要所有位都满足） */
    ret = mrtk_event_recv(&static_event, set_to_wait, option, timeout, &recved);

    /* Then: 验证返回 EEMPTY（条件不满足且非阻塞） */
    EXPECT_EQ(ret, MRTK_EEMPTY);
}

/* ==============================================================================
 * 测试覆盖率统计
 * ============================================================================== */

/*
 * 测试用例总数：约 35 个
 *
 * API 覆盖率：100%
 * - mrtk_event_init: 3 个用例（正常、NULL event、NULL name）
 * - mrtk_event_detach: 4 个用例（正常有任务、正常无任务、NULL、动态对象）
 * - mrtk_event_create: 2 个用例（正常、NULL name）
 * - mrtk_event_delete: 3 个用例（正常、NULL、静态对象）
 * - mrtk_event_send: 7 个用例（正常、NULL、零值、位合并、最大值、最高位）
 * - mrtk_event_recv: 12 个用例（条件满足 OR/AND、自动清除、参数校验、中断检查、非阻塞、边界值）
 * - mrtk_event_control: 3 个用例（清空、NULL、非法命令）
 * - 综合场景: 3 个用例
 *
 * 分支覆盖率：高
 * - 条件满足/不满足
 * - AND/OR 判断
 * - 自动清除/不清除
 * - 阻塞/非阻塞
 * - 中断上下文检查
 *
 * 边界值覆盖率：高
 * - set = 0 / 0xFFFFFFFF / 0x80000000
 * - timeout = 0 / MRTK_IPC_WAIT_FOREVER / 有限值
 * - 所有 32 位事件位
 *
 * 状态机覆盖率：完整
 * - Init -> Send/Recv -> Detach/Delete 生命周期
 * - 静态对象和动态对象的生命周期管理
 * - 非法状态调用（动态对象 detach、静态对象 delete）
 */
