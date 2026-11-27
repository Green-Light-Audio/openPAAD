/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * FreeRTOS configuration for the host simulator (FreeRTOS POSIX port).
 */

#ifndef OPENPAAD_SIM_FREERTOS_CONFIG_H
#define OPENPAAD_SIM_FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configCPU_CLOCK_HZ                      1000000000UL
#define configTICK_RATE_HZ                      1000
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_64_BITS
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                4096 /* words: host threads need more than PTHREAD_STACK_MIN */
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMAX_TASK_NAME_LEN                 20
#define configTOTAL_HEAP_SIZE                   (8 * 1024 * 1024)
#define configSUPPORT_STATIC_ALLOCATION         1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    1
#define configQUEUE_REGISTRY_SIZE               16
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                32
#define configTIMER_TASK_STACK_DEPTH            4096
#define configUSE_IDLE_HOOK                     1 /* sleeps: see hal/sim/src/freertos_hooks.c */
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                1
#define configGENERATE_RUN_TIME_STATS           0

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetHandle                  1

#define configASSERT(x)                    \
	do {                               \
		if (!(x))                  \
			__builtin_trap();  \
	} while (0)

#endif /* OPENPAAD_SIM_FREERTOS_CONFIG_H */
