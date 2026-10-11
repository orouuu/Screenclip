#pragma once

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/semphr.h>

#include "reglages.h"

class RgbDisplay {
 public:
  static constexpr int kPixels = PANEL_W * PANEL_H;

  bool init() {
    initPanelRegisters();

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = PANEL_W;
    cfg.timings.v_res = PANEL_H;
    cfg.timings.hsync_back_porch = 30;
    cfg.timings.hsync_front_porch = 30;
    cfg.timings.hsync_pulse_width = 6;
    cfg.timings.vsync_back_porch = 20;
    cfg.timings.vsync_front_porch = 20;
    cfg.timings.vsync_pulse_width = 40;
    cfg.timings.flags.pclk_active_neg = true;
    cfg.timings.flags.pclk_idle_high = true;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 2;
    cfg.bounce_buffer_size_px = 41 * PANEL_W;
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = PIN_HSYNC;
    cfg.vsync_gpio_num = PIN_VSYNC;
    cfg.de_gpio_num = PIN_DE;
    cfg.pclk_gpio_num = PIN_PCLK;
    cfg.disp_gpio_num = -1;
    cfg.flags.fb_in_psram = true;
    const int data[16] = {PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4,
                          PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
                          PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4};
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = data[i];

    if (esp_lcd_new_rgb_panel(&cfg, &panel_) != ESP_OK) return false;

    frameDone_ = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_frame_buf_complete = onFrameComplete;
    esp_lcd_rgb_panel_register_event_callbacks(panel_, &cbs, frameDone_);

    if (esp_lcd_panel_reset(panel_) != ESP_OK) return false;
    if (esp_lcd_panel_init(panel_) != ESP_OK) return false;
    void *fb0 = nullptr;
    void *fb1 = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 2, &fb0, &fb1) != ESP_OK) return false;
    fb_[0] = static_cast<uint16_t *>(fb0);
    fb_[1] = static_cast<uint16_t *>(fb1);
    memset(fb_[0], 0, kPixels * 2);
    memset(fb_[1], 0, kPixels * 2);
    back_ = 1;
    return true;
  }

  uint16_t *backBuffer() {
    if (pending_) {
      if (xSemaphoreTake(frameDone_, pdMS_TO_TICKS(60)) != pdTRUE) timeouts++;
      pending_ = false;
    }
    return fb_[back_];
  }

  const uint16_t *frontBuffer() const { return fb_[back_ ^ 1]; }

  void present() {
    backBuffer();
    xSemaphoreTake(frameDone_, 0);
    esp_lcd_panel_draw_bitmap(panel_, 0, 0, PANEL_W, PANEL_H, fb_[back_]);
    back_ ^= 1;
    pending_ = true;
  }

 private:
  esp_lcd_panel_handle_t panel_ = nullptr;
  SemaphoreHandle_t frameDone_ = nullptr;
  uint16_t *fb_[2] = {};
  int back_ = 1;
  bool pending_ = false;

 public:
  uint32_t timeouts = 0;

  static bool IRAM_ATTR onFrameComplete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                                        void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
    return woken == pdTRUE;
  }

  void writeBits(uint16_t data) {
    for (uint16_t mask = 0x100; mask; mask >>= 1) {
      digitalWrite(PIN_LCD_SCK, LOW);
      digitalWrite(PIN_LCD_MOSI, (data & mask) ? HIGH : LOW);
      digitalWrite(PIN_LCD_SCK, HIGH);
    }
  }

  void initPanelRegisters() {
    static constexpr uint8_t kDelay = 0x80;
    static constexpr uint8_t list[] = {
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xEF, 1, 0x08,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
        0xC0, 2, 0xE5, 0x02,
        0xC1, 2, 0x15, 0x0A,
        0xC2, 2, 0x07, 0x02,
        0xCC, 1, 0x10,
        0xB0, 16, 0x00, 0x08, 0x51, 0x0D, 0xCE, 0x06, 0x00, 0x08,
                  0x08, 0x24, 0x05, 0xD0, 0x0F, 0x6F, 0x36, 0x1F,
        0xB1, 16, 0x00, 0x10, 0x4F, 0x0C, 0x11, 0x05, 0x00, 0x07,
                  0x07, 0x18, 0x02, 0xD3, 0x11, 0x6E, 0x34, 0x1F,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
        0xB0, 1, 0x4D,
        0xB1, 1, 0x37,
        0xB2, 1, 0x87,
        0xB3, 1, 0x80,
        0xB5, 1, 0x4A,
        0xB7, 1, 0x85,
        0xB8, 1, 0x21,
        0xB9, 2, 0x00, 0x13,
        0xC0, 1, 0x09,
        0xC1, 1, 0x78,
        0xC2, 1, 0x78,
        0xD0, 1, 0x88,
        0xE0, 3 + kDelay, 0x80, 0x00, 0x02, 100,
        0xE1, 11, 0x0F, 0xA0, 0x00, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x60, 0x60,
        0xE2, 13, 0x30, 0x30, 0x60, 0x60, 0x45, 0xA0, 0x00, 0x00, 0x46, 0xA0, 0x00, 0x00, 0x00,
        0xE3, 4, 0x00, 0x00, 0x33, 0x33,
        0xE4, 2, 0x44, 0x44,
        0xE5, 16, 0x0F, 0x4A, 0xA0, 0xA0, 0x11, 0x4A, 0xA0, 0xA0,
                  0x13, 0x4A, 0xA0, 0xA0, 0x15, 0x4A, 0xA0, 0xA0,
        0xE6, 4, 0x00, 0x00, 0x33, 0x33,
        0xE7, 2, 0x44, 0x44,
        0xE8, 16, 0x10, 0x4A, 0xA0, 0xA0, 0x12, 0x4A, 0xA0, 0xA0,
                  0x14, 0x4A, 0xA0, 0xA0, 0x16, 0x4A, 0xA0, 0xA0,
        0xEB, 7, 0x02, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x00,
        0xED, 16, 0xFF, 0xFF, 0x04, 0x56, 0x72, 0xFF, 0xFF, 0xFF,
                  0xFF, 0xFF, 0xFF, 0x27, 0x65, 0x40, 0xFF, 0xFF,
        0xEF, 6, 0x08, 0x08, 0x08, 0x40, 0x3F, 0x64,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xE8, 2, 0x00, 0x0E,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
        0x11, 0 + kDelay, 120,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
        0xE8, 2 + kDelay, 0x00, 0x0C, 10,
        0xE8, 2, 0x00, 0x00,
        0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
        0x3A, 1, 0x55,
        0x36, 1, 0x00,
        0x35, 1, 0x00,
        0x29, 0 + kDelay, 20,
    };

    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_SCK, OUTPUT);
    pinMode(PIN_LCD_MOSI, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);
    digitalWrite(PIN_LCD_SCK, LOW);
    digitalWrite(PIN_LCD_MOSI, LOW);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(120);

    size_t i = 0;
    while (i < sizeof(list)) {
      const uint8_t cmd = list[i++];
      const uint8_t info = list[i++];
      const uint8_t len = info & 0x7F;
      digitalWrite(PIN_LCD_CS, LOW);
      writeBits(cmd);
      for (uint8_t k = 0; k < len; ++k) writeBits(0x100 | list[i++]);
      digitalWrite(PIN_LCD_CS, HIGH);
      if (info & kDelay) delay(list[i++]);
    }
  }
};
