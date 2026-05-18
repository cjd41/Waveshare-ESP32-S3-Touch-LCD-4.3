#include <Arduino.h>
#include <lvgl.h>
#include <esp_display_panel.hpp>
#include <ui.h>
#include "FS.h"
#include "SD_MMC.h"
#include "lvgl_v9_port.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

#define TP_RST      1
#define BL_PIN      2  // CH422G pin — LCD backlight, active HIGH
#define LCD_RST     3
#define USB_SEL_PIN 5  // CH422G pin — USB OTG select, must be held LOW

// SD card via SDMMC 1-bit mode (CS is on CH422G expander, so use native protocol instead)
#define SD_CMD  11  // SDMMC CMD  (was SPI MOSI)
#define SD_CLK  12  // SDMMC CLK
#define SD_D0   13  // SDMMC D0   (was SPI MISO)

static esp_expander::Base *g_io_base = nullptr;
static lv_obj_t *label              = nullptr;

static String listRootDir()
{
    // 1-bit SDMMC: CLK, CMD, D0 only — no CS pin needed
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    if (!SD_MMC.begin("/sdcard", /*mode1bit=*/true)) {
        return "SD card not found";
    }
    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        SD_MMC.end();
        return "Cannot open root directory";
    }
    String out;
    File entry = root.openNextFile();
    while (entry) {
        char line[64];
        if (entry.isDirectory()) {
            snprintf(line, sizeof(line), "[DIR]  %s\n", entry.name());
        } else {
            snprintf(line, sizeof(line), "%8lu B  %s\n",
                     (unsigned long)entry.size(), entry.name());
        }
        out += line;
        entry = root.openNextFile();
    }
    root.close();
    SD_MMC.end();
    if (out.isEmpty()) out = "(empty)";
    return out;
}

static void refreshListing(lv_event_t *e)
{
    String listing = listRootDir();
    lvgl_port_lock(-1);
    lv_label_set_text(label, listing.c_str());
    lvgl_port_unlock();
}

void setBacklightPower(bool activeHigh)
{
    if (g_io_base) g_io_base->multiDigitalWrite(1 << BL_PIN, activeHigh);
}

void setup()
{
    Serial.begin(115200);

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif

    assert(board->begin());

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

    static lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    static lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, lv_color_white());
    lv_style_set_text_font(&style_text, &lv_font_montserrat_16);

    label = lv_label_create(screen);
    lv_obj_set_pos(label, 10, 10);
    lv_obj_set_size(label, 700, 440);
    lv_obj_add_style(label, &style_text, LV_PART_MAIN);
    lv_label_set_text(label, "Reading SD card...");

    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_add_event_cb(btn, refreshListing, LV_EVENT_PRESSED, 0);
    lv_obj_set_size(btn, 80, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_obj_add_style(btn_label, &style_text, LV_PART_MAIN);
    lv_obj_align(btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(btn_label, "Refresh");

    lv_scr_load(screen);
    lvgl_port_unlock();

    // Read SD card after UI is visible
    String listing = listRootDir();
    lvgl_port_lock(-1);
    lv_label_set_text(label, listing.c_str());
    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    delay(1000);
}
