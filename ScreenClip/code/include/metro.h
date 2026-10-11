#include <Arduino.h>
#include <time.h>

#include "reglages.h"
#include "fonts_data.h"
#include "metro_horaires.h"
#include "texte.h"
#include "vista.h"

namespace metro {
inline int lineIndex = kMetroDefaultLine;

inline time_t startEpoch = 1791262800;

inline void begin() {
  setenv("TZ", TIMEZONE, 1);
  tzset();
  startEpoch = 1791262800 + esp_random() % (14 * 3600);
}

inline time_t nowEpoch() { return startEpoch + millis() / 1000; }

inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352D;
  x ^= x >> 15; x *= 0x846CA68B;
  return x ^ (x >> 16);
}

struct Board {
  bool closed = false;
  int firstMinute = -1;
  int count = 0;
  time_t departures[2] = {};
  bool dots[2] = {};
};

inline uint32_t enteredAtMs = 0;

inline uint32_t helpShownAtMs = 0;
inline bool helpActive = false;

inline void enter() {
  enteredAtMs = millis();
  helpShownAtMs = millis();
  helpActive = METRO_HELP_MS > 0;
}

inline bool helpVisible() {
  if (helpActive && millis() - helpShownAtMs >= METRO_HELP_MS) helpActive = false;
  return helpActive;
}

inline void nextLine() {
  helpActive = false;
  lineIndex = (lineIndex + 1) % kMetroLineCount;
  enteredAtMs = millis();
}

struct ServiceDay {
  time_t midnight;
  int type;
};

inline ServiceDay serviceDay(time_t now) {
  time_t shifted = now - 4 * 3600;
  struct tm local;
  localtime_r(&shifted, &local);
  const int weekday = local.tm_wday;
  local.tm_hour = local.tm_min = local.tm_sec = 0;
  local.tm_isdst = -1;
  return {mktime(&local), weekday == 0 ? 3 : (weekday == 6 ? 2 : (weekday == 5 ? 1 : 0))};
}

inline Board computeDepartures(time_t now) {
  Board b;
  const MetroLine &line = kMetroLines[lineIndex];
  const ServiceDay day = serviceDay(now);
  const uint16_t *times = line.times[day.type];
  const int count = line.count[day.type];
  const long t = (long)(now - day.midnight);

  int lo = 0, hi = count;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if ((long)times[mid] * 2 < t - METRO_DWELL_S) lo = mid + 1; else hi = mid;
  }
  for (int i = lo; i < count && b.count < 2; ++i) b.departures[b.count++] = day.midnight + (time_t)times[i] * 2;

  if (b.count == 0 || b.departures[0] - now > 3600) {
    b.closed = true;
    if (b.count > 0) {
      b.firstMinute = times[lo] * 2 / 60;
    } else {
      const ServiceDay next = serviceDay(day.midnight + 30 * 3600);
      if (line.count[next.type] > 0) b.firstMinute = line.times[next.type][0] * 2 / 60;
    }
    b.count = 0;
  }
  return b;
}

inline void applyDots(Board &b, time_t now) {
  if (millis() - enteredAtMs < METRO_START_DOTS_MS) {
    b.dots[0] = b.dots[1] = true;
    return;
  }

  const uint32_t window = (uint32_t)(now / 40);
  const uint32_t h = hash32(window * 2246822519u);
  if (h % 4 != 0) return;
  const int startS = (h >> 8) % 30;
  const int lengthS = 4 + (h >> 16) % 5;
  const int inWindow = (int)(now % 40);
  if (inWindow < startS || inWindow >= startS + lengthS) return;
  const int which = (h >> 24) & 1;

  if (which < b.count && b.departures[which] - now > 45) b.dots[which] = true;
}

inline Board compute(time_t now) {
  static time_t cachedAt = -1;
  static int cachedLine = -1;
  static Board cached;
  if (now != cachedAt || lineIndex != cachedLine) {
    cached = computeDepartures(now);
    cachedAt = now;
    cachedLine = lineIndex;
  }
  Board b = cached;
  if (!b.closed) applyDots(b, now);
  return b;
}

inline uint32_t signature() {
  const time_t now = nowEpoch();
  const Board b = compute(now);
  uint32_t h = b.closed * 7 + lineIndex * 16 + helpVisible() * 8;
  h = h * 131 + (uint32_t)(now / 60);
  for (int i = 0; i < b.count; ++i) {
    const long seconds = (long)(b.departures[i] - now);
    h = h * 131 + (uint32_t)(seconds / 60 + 1000);
    if (b.dots[i]) h = h * 131 + 5 + (millis() / 300) % 3;
    else if (seconds < 0) h = h * 131 + (millis() / 500) % 2;
  }
  return h & 0x0FFFFFFF;
}

constexpr uint16_t kYellow = 0xFDE0;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kNavy = 0x09D1;
constexpr int kHeaderH = 84;
constexpr int kStripeY = 302;

inline void drawCentered(Canvas &c, const Font &font, int cx, int y, uint16_t color, const char *text,
                         uint8_t opacity = 255) {
  drawText(c, font, cx - textWidth(font, text) / 2, y, color, text, opacity);
}

inline void drawHeader(Canvas &c) {
  const MetroLine &line = kMetroLines[lineIndex];
  const uint16_t lineColor = rgb565(line.color >> 16, line.color >> 8, line.color);
  const uint16_t numberColor = line.darkText ? 0x0000 : kWhite;
  c.fillRect(0, 0, c.w, kHeaderH, kWhite);
  c.fillRect(0, kStripeY, c.w, c.h - kStripeY, lineColor);
  const int cy = kHeaderH / 2;

  fillCircle(c, 50, cy, 30, kNavy, 25);
  drawCentered(c, kFontLine, 50, cy + 13, kNavy, "M");

  fillCircle(c, 122, cy, 30, lineColor);
  if (line.suffix[0]) {
    const int width = textWidth(kFontLine, line.number) + textWidth(kFontSup, line.suffix);
    const int x = drawText(c, kFontLine, 122 - width / 2, cy + 13, numberColor, line.number);
    drawText(c, kFontSup, x, cy + 13, numberColor, line.suffix);
  } else {
    drawCentered(c, kFontLine, 122, cy + 13, numberColor, line.number);
  }

  const bool fits = 170 + textWidth(kFontHeader, line.destination) < c.w - 16;
  drawText(c, fits ? kFontHeader : kFontLine, 170, cy + (fits ? 20 : 13), kNavy, line.destination);
}

inline void drawHelp(Canvas &c) {
  static const uint8_t rim[3] = {118, 170, 212}, glassTop[3] = {252, 254, 255}, glassBottom[3] = {206, 228, 246};
  static const uint8_t white[3] = {255, 255, 255};
  static const char *const kRows[][2] = {
      {"Deux appuis", "change de ligne"},
      {"Un appui", "image suivante"},
      {"Appui long", "image précédente"},
  };
  constexpr uint16_t kBlue = 0x2B97, kInk = 0x2965;

  constexpr int w = 318, h = 78;
  const int x = c.w - 4 - w, y = 3;
  vista::roundRect(c, x, y, w, h, 13, rim, rim, 245);
  vista::roundRect(c, x + 2, y + 2, w - 4, h - 4, 11, glassTop, glassBottom, 245);
  vista::roundRect(c, x + 5, y + 4, w - 10, h * 42 / 100, 9, white, white, 120);
  for (int i = 0; i < 3; ++i) {
    const int baseline = y + 25 + i * 22;
    drawText(c, kFontVistaSmall, x + 20, baseline, kBlue, kRows[i][0]);
    drawText(c, kFontVistaSmall, x + 152, baseline, kInk, kRows[i][1]);
  }
}

inline void drawBoard(Canvas &c);

inline void draw(Canvas &c) {
  drawBoard(c);
  if (helpVisible()) drawHelp(c);
}

inline void drawBoard(Canvas &c) {
  const time_t now = nowEpoch();
  const Board b = compute(now);

  c.fill(0);
  drawHeader(c);

  const int midY = (kHeaderH + kStripeY) / 2;
  if (b.closed) {
    drawCentered(c, kFontTitle, c.w / 2, midY, kWhite, "Service terminé");
    if (b.firstMinute >= 0) {
      char text[40];
      snprintf(text, sizeof(text), "Premier métro à %d:%02d", b.firstMinute / 60 % 24, b.firstMinute % 60);
      drawCentered(c, kFontLabel, c.w / 2, midY + 38, kGrey, text);
    }
    return;
  }

  const int half = c.w / 2;
  const int baseline = 282;
  for (int i = 0; i < b.count; ++i) {
    const long seconds = (long)(b.departures[i] - now);
    const int left = i * half;

    int lx = drawText(c, kFontLabel, left + 28, kHeaderH + 36, kWhite, i == 0 ? "1" : "2");
    lx = drawText(c, kFontSup, lx, kHeaderH + 27, kWhite, i == 0 ? "er" : "e");
    drawText(c, kFontLabel, lx, kHeaderH + 36, kWhite, " métro");

    if (b.dots[i]) {
      const int phase = (millis() / 300) % 3;
      for (int k = 0; k < 3; ++k) {
        fillCircle(c, left + half / 2 + (k - 1) * 46, baseline - 16, 12, kYellow, 0, k == phase ? 255 : 90);
      }
      continue;
    }

    char text[8];
    snprintf(text, sizeof(text), "%ld", seconds < 0 ? 0 : seconds / 60);
    const bool blink = seconds < 0 && (millis() / 500) % 2;
    drawCentered(c, kFontBig, left + half / 2, baseline, kYellow, text, blink ? 128 : 255);
  }
  c.fillRect(half - 2, kHeaderH + 62, 4, baseline - (kHeaderH + 62), kWhite);
}
}
