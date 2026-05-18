/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"

#include "esp_timer.h"
#undef ESP_UTILS_LOG_TAG
#define ESP_UTILS_LOG_TAG "LvPort"
#include "esp_lib_utils.h"
#include "lvgl_v9_port.h"

using namespace esp_panel::drivers;

static SemaphoreHandle_t  lvgl_mux         = nullptr;
static TaskHandle_t       lvgl_task_handle = nullptr;
static esp_timer_handle_t lvgl_tick_timer  = NULL;
static void *lvgl_buf[2]                   = {};

/* --------------------------------------------------------------------------
 * Flush callback — LVGL 9 signature takes lv_display_t *, not lv_disp_drv_t *.
 * Mode 3 (direct): LVGL renders directly into the LCD frame buffers.
 * On the last flush of each refresh cycle, switch to the updated frame buffer
 * and block until the VSYNC ISR confirms the previous frame finished.
 * -------------------------------------------------------------------------- */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
#if LVGL_PORT_AVOID_TEAR && LVGL_PORT_DIRECT_MODE
    LCD *lcd = (LCD *)lv_display_get_user_data(disp);
    if (lv_display_flush_is_last(disp)) {
        lcd->switchFrameBufferTo(px_map);
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
#else
    LCD *lcd = (LCD *)lv_display_get_user_data(disp);
    const int x1 = area->x1, x2 = area->x2, y1 = area->y1, y2 = area->y2;
    lcd->drawBitmap(x1, y1, x2 - x1 + 1, y2 - y1 + 1, (const uint8_t *)px_map);
#endif
    lv_display_flush_ready(disp);
}

/* VSYNC ISR: notifies the LVGL task that the previous frame finished transmitting */
IRAM_ATTR static bool onLcdVsyncCallback(void *user_data)
{
    BaseType_t need_yield = pdFALSE;
    TaskHandle_t task_handle = (TaskHandle_t)user_data;
    xTaskNotifyFromISR(task_handle, ULONG_MAX, eNoAction, &need_yield);
    return (need_yield == pdTRUE);
}

/* Called when drawBitmap completes — only used for non-RGB (SPI/QSPI) buses */
IRAM_ATTR static bool onDrawBitmapFinishCallback(void *user_data)
{
    lv_display_t *disp = (lv_display_t *)user_data;
    lv_display_flush_ready(disp);
    return false;
}

static lv_display_t *display_init(LCD *lcd)
{
    ESP_UTILS_CHECK_FALSE_RETURN(lcd != nullptr, nullptr, "Invalid LCD device");
    ESP_UTILS_CHECK_FALSE_RETURN(lcd->getRefreshPanelHandle() != nullptr, nullptr,
                                 "LCD device is not initialized");

    auto lcd_width  = lcd->getFrameWidth();
    auto lcd_height = lcd->getFrameHeight();

    lv_display_t *disp = lv_display_create(lcd_width, lcd_height);
    ESP_UTILS_CHECK_NULL_RETURN(disp, nullptr, "lv_display_create failed");

    lv_display_set_flush_cb(disp, flush_callback);
    lv_display_set_user_data(disp, lcd);

#if LVGL_PORT_AVOID_TEAR
    /* Direct / full-refresh mode: hand LVGL the actual LCD frame buffers */
    for (int i = 0; i < LVGL_PORT_DISP_BUFFER_NUM && i < 2; i++) {
        lvgl_buf[i] = lcd->getFrameBufferByIndex(i);
    }
    uint32_t buf_size = (uint32_t)lcd_width * lcd_height * sizeof(lv_color_t);
#if LVGL_PORT_DIRECT_MODE
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buf_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buf_size,
                           LV_DISPLAY_RENDER_MODE_FULL);
#endif
#else
    /* Partial-refresh mode: allocate small SRAM scratch buffers */
    uint32_t buf_size = (uint32_t)lcd_width * LVGL_PORT_BUFFER_SIZE_HEIGHT;
    for (int i = 0; i < LVGL_PORT_BUFFER_NUM && i < 2; i++) {
        lvgl_buf[i] = heap_caps_malloc(buf_size * sizeof(lv_color_t),
                                       LVGL_PORT_BUFFER_MALLOC_CAPS);
        assert(lvgl_buf[i]);
    }
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1],
                           buf_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    return disp;
}

/* --------------------------------------------------------------------------
 * Touch
 * -------------------------------------------------------------------------- */
static SemaphoreHandle_t touch_detected;

/* LVGL 9 read callback: takes lv_indev_t *, not lv_indev_drv_t * */
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    Touch *tp = (Touch *)lv_indev_get_user_data(indev);
    TouchPoint point;
    data->state = LV_INDEV_STATE_RELEASED;

    if (tp->isInterruptEnabled() && (xSemaphoreTake(touch_detected, 0) == pdFALSE)) {
        return;
    }

    if (tp->readPoints(&point, 1, 0) > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    }
}

static bool onTouchInterruptCallback(void *user_data)
{
    BaseType_t higher = pdFALSE;
    xSemaphoreGiveFromISR(touch_detected, &higher);
    portYIELD_FROM_ISR(higher);
    return false;
}

static lv_indev_t *indev_init(Touch *tp)
{
    ESP_UTILS_CHECK_FALSE_RETURN(tp != nullptr, nullptr, "Invalid touch device");
    ESP_UTILS_CHECK_FALSE_RETURN(tp->getPanelHandle() != nullptr, nullptr,
                                 "Touch device is not initialized");

    if (tp->isInterruptEnabled()) {
        touch_detected = xSemaphoreCreateBinary();
        tp->attachInterruptCallback(onTouchInterruptCallback, tp);
    }

    lv_indev_t *indev = lv_indev_create();
    ESP_UTILS_CHECK_NULL_RETURN(indev, nullptr, "lv_indev_create failed");
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, tp);
    return indev;
}

/* --------------------------------------------------------------------------
 * Tick
 * -------------------------------------------------------------------------- */
#if !LV_TICK_CUSTOM
static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static bool tick_init(void)
{
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick"
    };
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer), false,
        "Create LVGL tick timer failed");
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000), false,
        "Start LVGL tick timer failed");
    return true;
}

static bool tick_deinit(void)
{
    ESP_UTILS_CHECK_ERROR_RETURN(esp_timer_stop(lvgl_tick_timer),   false, "Stop tick failed");
    ESP_UTILS_CHECK_ERROR_RETURN(esp_timer_delete(lvgl_tick_timer), false, "Delete tick failed");
    return true;
}
#endif

/* --------------------------------------------------------------------------
 * LVGL task
 * -------------------------------------------------------------------------- */
static void lvgl_port_task(void *arg)
{
    ESP_UTILS_LOGD("Starting LVGL task");
    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
bool lvgl_port_init(LCD *lcd, Touch *tp)
{
    ESP_UTILS_CHECK_FALSE_RETURN(lcd != nullptr, false, "Invalid LCD device");

    auto bus_type = lcd->getBus()->getBasicAttributes().type;
#if LVGL_PORT_AVOID_TEAR
    ESP_UTILS_CHECK_FALSE_RETURN(
        (bus_type == ESP_PANEL_BUS_TYPE_RGB) || (bus_type == ESP_PANEL_BUS_TYPE_MIPI_DSI),
        false, "Avoid tearing only works with RGB/MIPI-DSI LCD");
    ESP_UTILS_LOGI("Avoid tearing enabled, mode: %d", LVGL_PORT_AVOID_TEARING_MODE);
#endif

    lv_init();
#if !LV_TICK_CUSTOM
    ESP_UTILS_CHECK_FALSE_RETURN(tick_init(), false, "Initialize LVGL tick failed");
#endif

    ESP_UTILS_LOGI("Initializing LVGL display driver");
    lv_display_t *disp = display_init(lcd);
    ESP_UTILS_CHECK_NULL_RETURN(disp, false, "Initialize LVGL display driver failed");

    if (bus_type != ESP_PANEL_BUS_TYPE_RGB) {
        ESP_UTILS_LOGD("Attach refresh finish callback to LCD");
        lcd->attachDrawBitmapFinishCallback(onDrawBitmapFinishCallback, (void *)disp);
    }

    if (tp != nullptr) {
        ESP_UTILS_LOGD("Initialize LVGL input driver");
        lv_indev_t *indev = indev_init(tp);
        ESP_UTILS_CHECK_NULL_RETURN(indev, false, "Initialize LVGL input driver failed");
    }

    ESP_UTILS_LOGD("Create mutex for LVGL");
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "Create LVGL mutex failed");

    ESP_UTILS_LOGD("Create LVGL task");
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE,
                         NULL, LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    ESP_UTILS_CHECK_FALSE_RETURN(ret == pdPASS, false, "Create LVGL task failed");

#if LVGL_PORT_AVOID_TEAR
    lcd->attachRefreshFinishCallback(onLcdVsyncCallback, (void *)lvgl_task_handle);
#endif

    return true;
}

bool lvgl_port_lock(int timeout_ms)
{
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "LVGL mutex is not initialized");
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE);
}

bool lvgl_port_unlock(void)
{
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "LVGL mutex is not initialized");
    xSemaphoreGiveRecursive(lvgl_mux);
    return true;
}

bool lvgl_port_deinit(void)
{
#if !LV_TICK_CUSTOM
    ESP_UTILS_CHECK_FALSE_RETURN(tick_deinit(), false, "Deinitialize LVGL tick failed");
#endif
    ESP_UTILS_CHECK_FALSE_RETURN(lvgl_port_lock(-1), false, "Lock LVGL failed");
    if (lvgl_task_handle != nullptr) {
        vTaskDelete(lvgl_task_handle);
        lvgl_task_handle = nullptr;
    }
    lvgl_port_unlock();
    lv_deinit();
#if !LVGL_PORT_AVOID_TEAR
    for (int i = 0; i < 2; i++) {
        if (lvgl_buf[i]) { free(lvgl_buf[i]); lvgl_buf[i] = nullptr; }
    }
#endif
    if (lvgl_mux) { vSemaphoreDelete(lvgl_mux); lvgl_mux = nullptr; }
    return true;
}
