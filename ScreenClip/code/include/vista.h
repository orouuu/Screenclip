#pragma once

#include <Arduino.h>

#include "reglages.h"
#include "fonts_data.h"
#include "logo_image.h"
#include "texte.h"

namespace vista {
constexpr uint32_t kBootMs = 3500;

inline uint16_t *cache = nullptr;
inline bool cached = false;

inline int lerp(int a, int b, float t) { return a + (int)((b - a) * t); }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline uint16_t dithered(float r, float g, float b, int x, int y) {
  static const int8_t kBayer[4][4] = {{-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}};
  const float d = kBayer[y & 3][x & 3] / 16.0f;
  const int ri = (int)(r + d * 8 + 0.5f), gi = (int)(g + d * 4 + 0.5f), bi = (int)(b + d * 8 + 0.5f);
  return rgb565(ri < 0 ? 0 : (ri > 255 ? 255 : ri), gi < 0 ? 0 : (gi > 255 ? 255 : gi), bi < 0 ? 0 : (bi > 255 ? 255 : bi));
}

inline uint16_t lightPixel(int x, int y, int w, int h) {
  const float v = (float)y / h;
  float r, g, b;
  if (v < 0.5f) {
    const float t = v / 0.5f;
    r = lerp(150, 250, t); g = lerp(203, 253, t); b = lerp(240, 255, t);
  } else {
    const float t = (v - 0.5f) / 0.5f;
    r = lerp(250, 196, t); g = lerp(253, 226, t); b = lerp(255, 247, t);
  }
  const float angle = atan2f(y + 70.0f, x - w * 0.22f);
  float ray = 0.5f + 0.5f * sinf(angle * 15.0f);
  ray = ray * ray * (1 - v * 0.8f) * 0.55f;
  r += (255 - r) * ray; g += (255 - g) * ray; b += (255 - b) * ray;
  return dithered(r, g, b, x, y);
}

inline void background(Canvas &c) {
  if (!cache) cache = static_cast<uint16_t *>(heap_caps_malloc(c.w * c.h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!cache) { c.fill(0xDF7F); return; }
  if (!cached) {
    for (int y = 0; y < c.h; ++y) {
      for (int x = 0; x < c.w; ++x) cache[y * c.w + x] = lightPixel(x, y, c.w, c.h);
    }
    cached = true;
  }
  memcpy(c.pixels, cache, c.w * c.h * 2);
}

inline void roundRect(Canvas &c, int x, int y, int w, int h, float radius, const uint8_t top[3],
                      const uint8_t bottom[3], uint8_t opacity = 255) {
  const float hx = w / 2.0f, hy = h / 2.0f, cx = x + hx, cy = y + hy;
  for (int py = y - 1; py <= y + h; ++py) {
    if (py < 0 || py >= c.h) continue;
    const float t = clamp01((py - y) / (float)h);
    const uint16_t color = rgb565(lerp(top[0], bottom[0], t), lerp(top[1], bottom[1], t), lerp(top[2], bottom[2], t));
    for (int px = x - 1; px <= x + w; ++px) {
      if (px < 0 || px >= c.w) continue;
      const float qx = fabsf(px + 0.5f - cx) - (hx - radius), qy = fabsf(py + 0.5f - cy) - (hy - radius);
      const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
      const float dist = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - radius;
      const float a = clamp01(0.5f - dist);
      if (a <= 0) continue;
      uint16_t &p = c.pixels[py * c.w + px];
      p = blend565(p, color, (uint8_t)(a * opacity));
    }
  }
}

inline void glassFrame(Canvas &c, int x, int y, int size) {
  static const uint8_t glow[3] = {120, 190, 235}, rimTop[3] = {178, 216, 240}, rimBottom[3] = {120, 178, 220};
  static const uint8_t edge[3] = {96, 150, 196}, panelTop[3] = {250, 252, 253}, panelBottom[3] = {196, 206, 214};
  static const uint8_t white[3] = {255, 255, 255};
  roundRect(c, x - 6, y - 5, size + 12, size + 14, 20, glow, glow, 70);
  roundRect(c, x, y, size, size, 15, rimTop, rimBottom);
  roundRect(c, x + 9, y + 9, size - 18, size - 18, 8, edge, edge);
  roundRect(c, x + 11, y + 11, size - 22, size - 22, 7, panelTop, panelBottom);
  roundRect(c, x + 3, y + 3, size - 6, size * 46 / 100, 12, white, white, 70);
}

inline void busyRing(Canvas &c, int cx, int cy, float radius, float thickness, uint16_t color, uint32_t ms) {
  const float head = ms * (2 * PI / 1200.0f);
  const int r = (int)(radius + thickness) + 1;
  for (int py = cy - r; py <= cy + r; ++py) {
    if (py < 0 || py >= c.h) continue;
    for (int px = cx - r; px <= cx + r; ++px) {
      if (px < 0 || px >= c.w) continue;
      const float dx = px - cx, dy = py - cy;
      const float cover = clamp01(thickness / 2 + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - radius));
      if (cover <= 0) continue;
      float turn = (head - atan2f(dy, dx)) / (2 * PI);
      turn -= floorf(turn);
      const float tail = 0.12f + 0.88f * (1 - turn) * (1 - turn);
      uint16_t &p = c.pixels[py * c.w + px];
      p = blend565(p, color, (uint8_t)(cover * tail * 255));
    }
  }
}

inline void drawBoot(Canvas &c, uint32_t ms) {
  background(c);
  const uint8_t logoOpacity = ms < 200 ? 0 : (ms < 900 ? (ms - 200) * 255 / 700 : 255);
  const int logoX = (c.w - kLogoW) / 2, logoY = 58 - kLogoPad;
  drawImage(c, logoX, logoY, kLogoW, kLogoH, kLogoColor, kLogoAlpha, logoOpacity);

  drawText(c, kFontVistaMedium, 26, c.h - 24, rgb565(42, 112, 184), BOOT_BYLINE, logoOpacity);
  if (ms > 600) busyRing(c, c.w / 2, 272, 19, 5, rgb565(46, 150, 222), ms);
}

inline void drawMessage(Canvas &c, const char *title, const char *text, int32_t spinnerMs) {
  constexpr uint16_t kBlue = 0x2B97;
  constexpr uint16_t kInk = 0x2965;
  background(c);
  constexpr int kFrame = 136;
  const int fx = 84, fy = (c.h - kFrame) / 2;
  glassFrame(c, fx, fy, kFrame);
  if (spinnerMs >= 0) {
    busyRing(c, fx + kFrame / 2, fy + kFrame / 2, 30, 8, rgb565(46, 150, 222), spinnerMs);
  } else {
    drawText(c, kFontVistaTitle, fx + (kFrame - textWidth(kFontVistaTitle, "!")) / 2, fy + kFrame / 2 + 21, kBlue, "!");
  }
  const int tx = fx + kFrame + 44;
  drawText(c, kFontVistaTitle, tx, c.h / 2 - (text ? 4 : -20), kBlue, title);
  if (text) drawText(c, kFontVistaText, tx + 2, c.h / 2 + 42, kInk, text);
}

inline void drawHelp(Canvas &c) {
  constexpr uint16_t kBlue = 0x2B97;
  constexpr uint16_t kInk = 0x2965;
  static const char *const kRows[][2] = {
      {"Un appui", "image suivante"},
      {"Appui long", "image précédente"},
      {"Deux appuis", "tourne l'écran"},
      {"Deux appuis sur le métro", "change de ligne"},
      {"Branchée à un ordinateur", "devient une clé USB pour tes GIF"},
  };
  background(c);
  const int left = 56;
  drawText(c, kFontVistaTitle, left, 80, kBlue, "Mode d'emploi");
  c.fillRect(left, 100, c.w - 2 * left, 1, rgb565(150, 196, 232));
  for (int i = 0; i < 5; ++i) {
    const int y = 142 + i * 37;
    drawText(c, kFontVistaText, left, y, kBlue, kRows[i][0]);
    drawText(c, kFontVistaText, left + 320, y, kInk, kRows[i][1]);
  }
}
}
