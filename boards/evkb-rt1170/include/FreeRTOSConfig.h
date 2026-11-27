/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * FreeRTOS kernel configuration for the i.MX RT1170 EVKB, Cortex-M7 core.
 *
 * Kept deliberately explicit: every option the kernel or the GenAVB OS layer
 * reads is set here, so behaviour does not depend on kernel defaults.
 */

#ifndef OPENPAAD_FREERTOS_CONFIG_H
#define OPENPAAD_FREERTOS_CONFIG_H

#if defined(__GNUC__) || defined(__ICCARM__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

/* Scheduler */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                ((unsigned short)256)
#define configMAX_TASK_NAME_LEN                 20
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               16
#define configUSE_QUEUE_SETS                    1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 2
#define configUSE_APPLICATION_TASK_TAG          0

/* Memory */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(256 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        1 /* ucHeap lives in OCRAM, see freertos_heap.c */

/* Hooks */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* Run-time statistics and tracing */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Software timers */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                32
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

/* Optional API */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_xSemaphoreGetMutexHolder        1

/* Cortex-M interrupt priorities (the RT1170 implements 4 priority bits) */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      ((1U << configPRIO_BITS) - 1)
/* Interrupts at a numerically lower (more urgent) priority than this one must
 * not call FreeRTOS API functions. The ENET and GPT interrupts used for media
 * timing run above it. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2

#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Assertions */
#ifndef __ASSEMBLER__
void openpaad_freertos_assert(const char *file, int line);
#endif
#define configASSERT(x)                                    \
	do {                                               \
		if (!(x))                                  \
			openpaad_freertos_assert(__FILE__, __LINE__); \
	} while (0)

/* Map the kernel handlers onto the CMSIS vector names */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* OPENPAAD_FREERTOS_CONFIG_H */
