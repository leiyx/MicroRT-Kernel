/**
 * @file mrtk_config.h
 * @author Leiyx
 * @brief MRTK (MicroRT-Kernel) 用户级系统配置文件 (对标 FreeRTOSConfig.h)
 * @details
 * 本文件是面向用户的纯粹配置表，仅包含宏定义，不包含任何逻辑判断。
 * 开发者通过修改此文件来裁剪内核功能、调整内存占用和系统行为。
 * @date 2026-02-28
 * @version 0.3
 */

#ifndef __MRTK_CONFIG_H__
#define __MRTK_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
 * 架构对齐配置 (Architecture Alignment)
 *----------------------------------------------------------------------------*/

/**< 内存对齐字节数。针对 Cortex-M4 FPU，必须为 8 字节对齐 */
#define MRTK_ALIGN_SIZE 4

/**< CPU 是否支持 CLZ 指令 (Count Leading Zeros)
 *  - Cortex-M3/M4/M7 支持 CLZ 指令，设为 1 可使用硬件加速
 *  - Cortex-M0/M0+ 不支持 CLZ 指令，必须设为 0 使用软件查找表
 *  - 启用后可提升调度器查找最高优先级的性能
 */
#define MRTK_CPU_HAS_CLZ 0

/*-----------------------------------------------------------------------------
 * 基础系统配置 (Basic System Configuration)
 *----------------------------------------------------------------------------*/

/**< 系统时钟节拍频率（单位：Hz），例如 1000 表示 1ms 一次 Tick 中断 */
#define MRTK_TICK_PER_SECOND 1000

/**< 系统支持的优先级数量 */
#define MRTK_MAX_PRIO_LEVEL_NUM 32

/**< 优先级方向配置 (1: 数值越小优先级越高, 0: 数值越大优先级越高)
 *
 * @note 默认采用数值越小优先级越高的策略（0 为最高优先级）
 */
#define MRTK_PRIO_HIGHER_IS_LOWER_VALUE 1

/**< 内核对象名称的最大长度（包含字符串结束符 '\0'） */
#define MRTK_NAME_MAX 16

/**< 是否开启时间片轮转调度 (0: 关闭, 1: 开启) */
#define MRTK_USING_TIME_SLICE 0

/*-----------------------------------------------------------------------------
 * 内存管理配置 (Memory Management Configuration)
 *----------------------------------------------------------------------------*/

/**< 是否开启动态堆内存分配器 (Heap) */
#define MRTK_USING_MEM_HEAP 1

/**< 系统内核堆内存的总大小（单位：字节）。仅在开启 Heap 时有效 */
#define MRTK_HEAP_SIZE (1024 * 32)

/**< 是否开启定长内存池分配器 (Memory Pool) */
#define MRTK_USING_MEM_POOL 1

/*-----------------------------------------------------------------------------
 * 进程间通信配置 (IPC Configuration)
 *----------------------------------------------------------------------------*/

/**< 是否开启信号量 (Semaphore) 支持 */
#define MRTK_USING_SEMAPHORE 1

/**< 是否开启互斥量 (Mutex) 支持 */
#define MRTK_USING_MUTEX 1

/**< 是否开启消息队列 (Message Queue) 支持 */
#define MRTK_USING_MESSAGE_QUEUE 1

/**< 是否开启邮箱 (Mailbox) 支持 */
#define MRTK_USING_MAILBOX 1

/**< 是否开启事件标志组 (Event Flags) 支持 */
#define MRTK_USING_EVENT 1

/*-----------------------------------------------------------------------------
 * 软件定时器配置 (Software Timer Configuration)
 *----------------------------------------------------------------------------*/

/**< 是否开启核心定时器功能 (包括硬定时器) */
#define MRTK_USING_TIMER 1

/**< 是否开启软定时器任务模式 (守护任务处理) */
#define MRTK_USING_TIMER_SOFT 1

/**< 内部软件定时器守护任务的优先级 (数值越小优先级越高) */
#define MRTK_TIMER_TASK_PRIO 8

/**< 内部软件定时器守护任务的栈大小（单位：字节） */
#define MRTK_TIMER_TASK_STACK_SIZE 512

/*-----------------------------------------------------------------------------
 * 调试与追踪配置 (Debugging & Tracing)
 *----------------------------------------------------------------------------*/

/**< 是否开启内核断言检查 (ASSERT) */
#define MRTK_USING_ASSERT 1

/**< 是否开启DUMP调试接口 */
#define MRTK_DEBUG 1

#ifdef __cplusplus
}
#endif

#endif /* __MRTK_CONFIG_H__ */