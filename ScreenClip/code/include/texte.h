#pragma once

#include <math.h>
#include <stdint.h>

struct Glyph {
  uint16_t code;
  uint8_t w, h;
  int8_t xoff;
  int16_t yoff;
  uint8_t advance;
  uint32_t offset;
};

struct Font {
  const Glyph *glyphs;
  uint16_t count;
  const uint8_t *data;
  uint8_t ascent;
  uint8_t lineHeight;
};

struct Canvas {
  uint16_t *pixels;
  int w, h;

  void fill(uint16_t color) {
    for (int i = 0; i < w * h; ++i) pixels[i] = color;
  }

  void fillRect(int x, int y, int rw, int rh, uint16_t color) {
    for (int yy = y < 0 ? 0 : y; yy < y + rh && yy < h; ++yy) {
      for (int xx = x < 0 ? 0 : x; xx < x + rw && xx < w; ++xx) pixels[yy * w + xx] = color;
    }
  }
};

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha) {
  if (alpha == 255) return fg;
  const uint32_t a = alpha + 1;
  const uint32_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (256 - a)) >> 8;
  const uint32_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (256 - a)) >> 8;
  const uint32_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (256 - a)) >> 8;
  return (r << 11) | (g << 5) | b;
}

inline uint16_t nextCodepoint(const char *&s) {
  const uint8_t c = *s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *s) {
    const uint16_t cp = ((c & 0x1F) << 6) | (*s & 0x3F);
    ++s;
    return cp;
  }
  if ((c & 0xF0) == 0xE0 && s[0] && s[1]) {
    const uint16_t cp = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
    s += 2;
    return cp;
  }
  return '?';
}

inline const Glyph *findGlyph(const Font &font, uint16_t code) {
  int lo = 0, hi = font.count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (font.glyphs[mid].code == code) return &font.glyphs[mid];
    if (font.glyphs[mid].code < code) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

inline void fillCircle(Canvas &c, float cx, float cy, float radius, uint16_t color, float inner = 0,
                       uint8_t opacity = 255) {
  const int r = (int)radius + 2;
  for (int y = (int)cy - r; y <= (int)cy + r; ++y) {
    if (y < 0 || y >= c.h) continue;
    for (int x = (int)cx - r; x <= (int)cx + r; ++x) {
      if (x < 0 || x >= c.w) continue;
      const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
      float a = radius + 0.5f - d;
      if (inner > 0) a = fminf(a, d - inner + 0.5f);
      if (a <= 0) continue;
      const uint8_t alpha = (uint8_t)((a >= 1 ? 255 : (int)(a * 255)) * (opacity + 1) >> 8);
      c.pixels[y * c.w + x] = blend565(c.pixels[y * c.w + x], color, alpha);
    }
  }
}

inline void drawImage(Canvas &c, int x, int y, int w, int h, const uint16_t *color, const uint8_t *alpha,
                      uint8_t opacity = 255) {
  for (int iy = 0; iy < h; ++iy) {
    const int py = y + iy;
    if (py < 0 || py >= c.h) continue;
    for (int ix = 0; ix < w; ++ix) {
      const int px = x + ix;
      if (px < 0 || px >= c.w) continue;
      const uint8_t a = (alpha[iy * w + ix] * (opacity + 1)) >> 8;
      if (a) c.pixels[py * c.w + px] = blend565(c.pixels[py * c.w + px], color[iy * w + ix], a);
    }
  }
}

inline int textWidth(const Font &font, const char *text) {
  int width = 0;
  while (*text) {
    if (const Glyph *g = findGlyph(font, nextCodepoint(text))) width += g->advance;
  }
  return width;
}

inline int drawText(Canvas &c, const Font &font, int x, int y, uint16_t color, const char *text,
                    uint8_t opacity = 255) {
  while (*text) {
    const Glyph *g = findGlyph(font, nextCodepoint(text));
    if (!g) continue;
    const uint8_t *alpha = font.data + g->offset;
    for (int gy = 0; gy < g->h; ++gy) {
      const int py = y + g->yoff + gy;
      if (py < 0 || py >= c.h) continue;
      for (int gx = 0; gx < g->w; ++gx) {
        const int px = x + g->xoff + gx;
        if (px < 0 || px >= c.w) continue;
        const uint8_t a = (alpha[gy * g->w + gx] * (opacity + 1)) >> 8;
        if (a) c.pixels[py * c.w + px] = blend565(c.pixels[py * c.w + px], color, a);
      }
    }
    x += g->advance;
  }
  return x;
}
