#include <Arduino.h>
#include <lvgl.h>
#include <esp_display_panel.hpp>
#include <ui.h>
#include "lvgl_v9_port.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

#define USB_SEL_PIN 5  // CH422G pin — USB OTG select, must be held LOW
#define BL_PIN      2  // CH422G pin — LCD backlight, active HIGH

static esp_expander::Base *g_io_base = nullptr;

void setup()
{
    Serial.begin(115200);

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();

    // Configure anti-tearing (mode 3: double-buffer + LVGL direct-mode) with bounce buffer.
    // The bounce buffer decouples RGB DMA from PSRAM, eliminating tearing and color artifacts.
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // 800 pixels * 10 rows = 8000-pixel bounce buffer (16 KB SRAM)
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif

    assert(board->begin());

    // Set USB_SEL LOW via the IO expander to prevent USB bus contention.
    // The board manages TP_RST, LCD_BL, LCD_RST internally.
    auto io_exp = board->getIO_Expander();
    if (io_exp != nullptr) {
        auto base = io_exp->getBase();
        if (base != nullptr) {
            base->multiPinMode(1 << USB_SEL_PIN, OUTPUT);
            base->multiDigitalWrite(1 << USB_SEL_PIN, LOW);
            g_io_base = base;
        }
    }

     setBacklightPower(HIGH);

    Serial.println("Initializing LVGL");
    assert(lvgl_port_init(board->getLCD(), board->getTouch()));

    Serial.println("Creating UI");
    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    Serial.println("Setup done");
}

void setBacklightPower(bool activeHigh)
{
    g_io_base->multiDigitalWrite(1 << BL_PIN, activeHigh);
}

void loop()
{
    sleep(1);
}
