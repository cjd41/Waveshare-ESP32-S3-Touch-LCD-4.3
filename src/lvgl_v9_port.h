/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "sdkconfig.h"
#ifdef CONFIG_ARDUINO_RUNNING_CORE
#include <Arduino.h>
#endif
#include "esp_display_panel.hpp"
#include "lvgl.h"

#define LVGL_PORT_TICK_PERIOD_MS        (2)
#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LVGL_PORT_BUFFER_SIZE_HEIGHT    (20)
#define LVGL_PORT_BUFFER_NUM            (2)
#define LVGL_PORT_TASK_MAX_DELAY_MS     (500)
#define LVGL_PORT_TASK_MIN_DELAY_MS     (2)
#define LVGL_PORT_TASK_STACK_SIZE       (6 * 1024)
#define LVGL_PORT_TASK_PRIORITY         (2)
#ifdef ARDUINO_RUNNING_CORE
#define LVGL_PORT_TASK_CORE             (ARDUINO_RUNNING_CORE)
#else
#define LVGL_PORT_TASK_CORE             (0)
#endif

/**
 * Anti-tearing mode for RGB LCD:
 *   0 = disabled
 *   1 = LCD double-buffer + LVGL full-refresh
 *   2 = LCD triple-buffer + LVGL full-refresh
 *   3 = LCD double-buffer + LVGL direct-mode (recommended)
 */
#define LVGL_PORT_AVOID_TEARING_MODE    (3)

#if LVGL_PORT_AVOID_TEARING_MODE != 0
#define LVGL_PORT_ROTATION_DEGREE       (0)
#define LVGL_PORT_AVOID_TEAR            (1)
#if LVGL_PORT_AVOID_TEARING_MODE == 1
    #define LVGL_PORT_DISP_BUFFER_NUM   (2)
    #define LVGL_PORT_FULL_REFRESH      (1)
#elif LVGL_PORT_AVOID_TEARING_MODE == 2
    #define LVGL_PORT_DISP_BUFFER_NUM   (3)
    #define LVGL_PORT_FULL_REFRESH      (1)
#elif LVGL_PORT_AVOID_TEARING_MODE == 3
    #define LVGL_PORT_DISP_BUFFER_NUM   (2)
    #define LVGL_PORT_DIRECT_MODE       (1)
#else
    #error "Invalid avoid tearing mode"
#endif
#if (LVGL_PORT_ROTATION_DEGREE != 0) && defined(LVGL_PORT_DISP_BUFFER_NUM)
    #undef LVGL_PORT_DISP_BUFFER_NUM
    #define LVGL_PORT_DISP_BUFFER_NUM   (3)
#endif
#endif /* LVGL_PORT_AVOID_TEARING_MODE */

#ifdef __cplusplus
extern "C" {
#endif

bool lvgl_port_init(esp_panel::drivers::LCD *lcd, esp_panel::drivers::Touch *tp);
bool lvgl_port_deinit(void);
bool lvgl_port_lock(int timeout_ms);
bool lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
