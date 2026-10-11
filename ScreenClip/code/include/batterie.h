#pragma once

#include <Arduino.h>
#include <tusb.h>

#include "reglages.h"
#include "fonts_data.h"
#include "texte.h"
#include "vista.h"

namespace battery {
inline int percentFromMillivolts(int mv) {
  static const struct { int mv, percent; } kCurve[] = {
      {3300, 0}, {3450, 5}, {3650, 20}, {3750, 40}, {3850, 60}, {4000, 80}, {4110, 100},
  };
  constexpr int kPoints = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv <= kCurve[0].mv) return 0;
  for (int i = 1; i < kPoints; ++i) {
    if (mv < kCurve[i].mv) {
      return kCurve[i - 1].percent + (mv - kCurve[i - 1].mv) * (kCurve[i].percent - kCurve[i - 1].percent) /
                                         (kCurve[i].mv - kCurve[i - 1].mv);
    }
  }
  return 100;
}

inline int measureMillivolts() {
  uint32_t total = 0;
  for (int i = 0; i < 16; ++i) total += analogReadMilliVolts(PIN_BATTERY_ADC);
  return (int)(total / 16) * 2;
}

constexpr int kBars = 5;
inline float smoothedMv = 0;
inline int bars = kBars;
inline bool onUsb = false;
inline bool present = false;
inline uint32_t nextSampleMs = 0;

inline bool usbConnected() { return tud_mounted() && !tud_suspended(); }

inline void update(bool force = false) {
  if (!force && (int32_t)(millis() - nextSampleMs) < 0) return;
  nextSampleMs = millis() + 2000;

  const bool usb = usbConnected();
  const int mv = measureMillivolts();

  if (usb != onUsb || (smoothedMv != 0 && fabsf(mv - smoothedMv) > 250)) {
    onUsb = usb;
    smoothedMv = 0;
    nextSampleMs = millis() + 1500;
    if (!force) return;
  }
  const bool reseed = smoothedMv == 0;
  smoothedMv = reseed ? mv : smoothedMv + (mv - smoothedMv) * 0.1f;
  present = smoothedMv >= 3000 && smoothedMv <= 4400;
  if (!present) return;

  const int percent = percentFromMillivolts((int)smoothedMv);
  const int target = std::min(kBars, (percent + 19) / 20);
  if (reseed) bars = target;
  else if (percent > bars * 20 + 4) bars = std::min(kBars, bars + 1);
  else if (percent < (bars - 1) * 20 - 4) bars = std::max(0, bars - 1);
}

constexpr int kW = 96, kH = 40, kMargin = 12;
inline uint16_t colorPixels[kW * kH];
inline uint16_t alphaPixels[kW * kH];
inline int pillWidth = kW;

inline void build() {
  update();

  constexpr int kBarW = 8, kBarGap = 2, kIconH = 22, kPad = 15;
  constexpr int kInnerW = kBars * kBarW + (kBars - 1) * kBarGap;
  constexpr int kIconW = kInnerW + 8;
  pillWidth = kPad + kIconW + 4 + kPad;
  const int ix = kPad, iy = (kH - kIconH) / 2;

  static const uint8_t white[3] = {255, 255, 255};
  static const uint8_t rim[3] = {118, 170, 212}, glassTop[3] = {250, 253, 255}, glassBottom[3] = {196, 222, 244};
  static const uint8_t shell[3] = {74, 92, 112}, cellTop[3] = {252, 253, 254}, cellBottom[3] = {214, 222, 230};
  static const uint8_t greenTop[3] = {150, 226, 92}, greenBottom[3] = {52, 160, 40};
  static const uint8_t redTop[3] = {246, 120, 96}, redBottom[3] = {200, 40, 32};
  static const uint8_t boltTop[3] = {255, 204, 64}, boltBottom[3] = {236, 150, 16};

  Canvas alpha = {alphaPixels, kW, kH};
  alpha.fill(0);
  vista::roundRect(alpha, 0, 0, pillWidth, kH, kH / 2.0f, white, white, BATTERY_PILL_OPACITY);
  vista::roundRect(alpha, ix, iy, kIconW, kIconH, 4, white, white);
  alpha.fillRect(ix + kIconW, iy + 6, 4, kIconH - 12, 0xFFFF);

  Canvas color = {colorPixels, kW, kH};
  color.fill(rgb565(rim[0], rim[1], rim[2]));
  vista::roundRect(color, 1, 1, pillWidth - 2, kH - 2, kH / 2.0f - 1, glassTop, glassBottom);
  vista::roundRect(color, 4, 3, pillWidth - 8, kH / 2 - 3, kH / 4.0f, white, white, 150);

  vista::roundRect(color, ix, iy, kIconW, kIconH, 4, shell, shell);
  color.fillRect(ix + kIconW, iy + 6, 4, kIconH - 12, rgb565(shell[0], shell[1], shell[2]));
  vista::roundRect(color, ix + 2, iy + 2, kIconW - 4, kIconH - 4, 2, cellTop, cellBottom);

  if (onUsb || !present) {
    static const char *const kBolt[] = {
        "......###.", ".....###..", "....###...", "...###....", "..########",
        "#########.", "....###...", "...###....", "..###.....", ".###......",
    };
    const int bx = ix + (kIconW - 10) / 2, by = iy + (kIconH - 15) / 2;
    for (int r = 0; r < 10; ++r) {
      const uint16_t c = rgb565(boltTop[0] + (boltBottom[0] - boltTop[0]) * r / 9, boltTop[1] + (boltBottom[1] - boltTop[1]) * r / 9,
                                boltTop[2] + (boltBottom[2] - boltTop[2]) * r / 9);
      for (int col = 0; col < 10; ++col) {
        if (kBolt[r][col] == '#') color.fillRect(bx + col, by + r * 15 / 10, 1, 2, c);
      }
    }
  } else {
    const bool low = bars <= 1;
    for (int k = 0; k < bars; ++k) {
      vista::roundRect(color, ix + 4 + k * (kBarW + kBarGap), iy + 4, kBarW, kIconH - 8, 1.5f, low ? redTop : greenTop,
                       low ? redBottom : greenBottom);
    }
  }
}

inline void composite(uint16_t *panel, int rotation) {
  const int sw = (rotation & 1) ? PANEL_H : PANEL_W;
  const int sh = (rotation & 1) ? PANEL_W : PANEL_H;
  const int x0 = sw - pillWidth - kMargin, y0 = sh - kH - kMargin;
  for (int oy = 0; oy < kH; ++oy) {
    for (int ox = 0; ox < pillWidth; ++ox) {
      const int a = ((alphaPixels[oy * kW + ox] >> 5) & 63) * 255 / 63;
      if (!a) continue;
      const int sx = x0 + ox, sy = y0 + oy;
      int px, py;
      switch (rotation & 3) {
        case 0: px = sx; py = sy; break;
        case 1: px = PANEL_W - 1 - sy; py = sx; break;
        case 2: px = PANEL_W - 1 - sx; py = PANEL_H - 1 - sy; break;
        default: px = sy; py = PANEL_H - 1 - sx; break;
      }
      if (px < 0 || px >= PANEL_W || py < 0 || py >= PANEL_H) continue;
      uint16_t &dst = panel[py * PANEL_W + px];
      dst = blend565(dst, colorPixels[oy * kW + ox], a);
    }
  }
}
}
