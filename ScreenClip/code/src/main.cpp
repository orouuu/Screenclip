#include <Arduino.h>
#include <AnimatedGIF.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <driver/ledc.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include <algorithm>
#include <vector>

#include "reglages.h"
#include "ecran.h"

#include "batterie.h"
#include "metro.h"
#include "texte.h"
#include "cle_usb.h"
#include "vista.h"

namespace {
RgbDisplay display;
AnimatedGIF gif;
Preferences prefs;

std::vector<String> gifFiles;
int currentIndex = 0;

uint8_t *gifData = nullptr;
bool gifOpen = false;
uint32_t nextFrameAt = 0;
uint8_t *gifCanvas = nullptr;
uint8_t *gifTurbo = nullptr;

const uint16_t *srcPixels = nullptr;
int gifSrcW = 1, gifSrcH = 1;
int gifDstW = 1, gifDstH = 1;
bool srcCover = true;

constexpr int kUiW = 820, kUiH = 320;
Canvas ui = {nullptr, kUiW, kUiH};
bool metroPage = false;
uint32_t metroSignature = 0;

uint32_t rowMap[PANEL_H];
uint32_t colMap[PANEL_W];

constexpr int kTileCols = 64;
uint16_t tile[PANEL_W * kTileCols];
uint16_t tileRow[PANEL_W];
uint32_t tileSrcRow[PANEL_W];
int tileRows = 0;

int rowFirst = 0, rowEnd = PANEL_H, colFirst = 0, colEnd = PANEL_W;

int rotation = SCREEN_ROTATION;
int screenW() { return (rotation & 1) ? PANEL_H : PANEL_W; }
int screenH() { return (rotation & 1) ? PANEL_W : PANEL_H; }

uint32_t overlayUntil = 0;
bool overlayShown = false;

void showBatteryOverlay(uint32_t durationMs) {
#if BATTERY_OVERLAY
  battery::build();
  overlayUntil = millis() + durationMs;
#endif
}

void compositeOverlay(uint16_t *panel) {
  if ((int32_t)(overlayUntil - millis()) <= 0) return;
  battery::composite(panel, rotation);
  overlayShown = true;
}

void initBacklight(uint8_t level) {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_3;
  timer.freq_hz = 50000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t ch = {};
  ch.gpio_num = PIN_BACKLIGHT;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_1;
  ch.timer_sel = LEDC_TIMER_3;
  ch.duty = 255 - level;
  ledc_channel_config(&ch);
}

struct ButtonEvent {
  uint32_t heldMs;
  uint32_t releasedAt;
};
QueueHandle_t bootEvents = nullptr;
volatile bool bootDown = false;

void bootButtonTask(void *) {
  bool pressed = false;
  uint32_t changedAt = 0, pressedAt = 0;
  for (;;) {
    const bool down = digitalRead(PIN_LCD_CS) == LOW;
    const uint32_t now = millis();
    if (down != pressed && now - changedAt >= 25) {
      pressed = down;
      changedAt = now;
      bootDown = down;
      if (down) {
        pressedAt = now;
      } else {
        const ButtonEvent event = {std::max<uint32_t>(1, now - pressedAt), now};
        xQueueSend(bootEvents, &event, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}

void startBootButton() {
  pinMode(PIN_LCD_CS, INPUT_PULLUP);
  bootEvents = xQueueCreate(8, sizeof(ButtonEvent));
  xTaskCreatePinnedToCore(bootButtonTask, "bouton", 2048, nullptr, 3, nullptr, 0);
}

void buildMaps();
void renderFrame();

void showUi() {
  srcPixels = ui.pixels;
  gifSrcW = kUiW;
  gifSrcH = kUiH;
  srcCover = false;
  buildMaps();
  renderFrame();
}

void showMessage(const char *line1, const char *line2 = nullptr, int32_t spinnerMs = -1) {
  vista::drawMessage(ui, line1, line2, spinnerMs);
  showUi();
}

bool hasGifExtension(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".gif");
}

void scanFolder(const char *folder) {
  File dir = SD_MMC.open(folder);
  if (!dir || !dir.isDirectory()) return;
  const String base = String(folder) == "/" ? "" : String(folder);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);

    if (!f.isDirectory() && !name.startsWith(".") && hasGifExtension(name)) {
      gifFiles.push_back(base + "/" + name);
    }
    f.close();
  }
  dir.close();
}

bool mountSd() {
  SD_MMC.end();
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) return false;
  return SD_MMC.begin("/sdcard", true);
}

void loadGifList() {
  gifFiles.clear();
  scanFolder(GIF_FOLDER);
  if (gifFiles.empty()) scanFolder("/");
  std::sort(gifFiles.begin(), gifFiles.end(),
            [](const String &a, const String &b) { return a.compareTo(b) < 0; });
}

void *allocInternal(size_t size) {
  if (size + 24 * 1024 > heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) return nullptr;
  return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void *allocFast(size_t size) {
  if (void *p = allocInternal(size)) return p;
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void buildMaps() {
  const int sw = screenW();
  const int sh = screenH();
  if ((gifSrcW * sh < gifSrcH * sw) == srcCover) {
    gifDstW = sw;
    gifDstH = gifSrcH * sw / gifSrcW;
  } else {
    gifDstH = sh;
    gifDstW = gifSrcW * sh / gifSrcH;
  }
  const int offX = (sw - gifDstW) / 2;
  const int offY = (sh - gifDstH) / 2;
  auto srcX = [&](int sx) { return (uint32_t)clampi((sx - offX) * gifSrcW / gifDstW, 0, gifSrcW - 1); };
  auto srcY = [&](int sy) { return (uint32_t)clampi((sy - offY) * gifSrcH / gifDstH, 0, gifSrcH - 1) * gifSrcW; };

  for (int py = 0; py < PANEL_H; ++py) {
    switch (rotation & 3) {
      case 0: rowMap[py] = srcY(py); break;
      case 1: rowMap[py] = srcX(py); break;
      case 2: rowMap[py] = srcY(PANEL_H - 1 - py); break;
      default: rowMap[py] = srcX(PANEL_H - 1 - py); break;
    }
  }
  for (int px = 0; px < PANEL_W; ++px) {
    switch (rotation & 3) {
      case 0: colMap[px] = srcX(px); break;
      case 1: colMap[px] = srcY(PANEL_W - 1 - px); break;
      case 2: colMap[px] = srcX(PANEL_W - 1 - px); break;
      default: colMap[px] = srcY(px); break;
    }
  }

  const int x0 = clampi(offX, 0, sw), x1 = clampi(offX + gifDstW, 0, sw);
  const int y0 = clampi(offY, 0, sh), y1 = clampi(offY + gifDstH, 0, sh);
  switch (rotation & 3) {
    case 0: rowFirst = y0; rowEnd = y1; colFirst = x0; colEnd = x1; break;
    case 1: rowFirst = x0; rowEnd = x1; colFirst = PANEL_W - y1; colEnd = PANEL_W - y0; break;
    case 2: rowFirst = PANEL_H - y1; rowEnd = PANEL_H - y0; colFirst = PANEL_W - x1; colEnd = PANEL_W - x0; break;
    default: rowFirst = PANEL_H - x1; rowEnd = PANEL_H - x0; colFirst = y0; colEnd = y1; break;
  }

  tileRows = 0;
  for (int px = 0; px < PANEL_W; ++px) {
    if (px == 0 || colMap[px] != colMap[px - 1]) tileSrcRow[tileRows++] = colMap[px];
    tileRow[px] = tileRows - 1;
  }
}

void renderRotated(const uint16_t *canvas, uint16_t *dst) {
  int py = 0;
  while (py < PANEL_H) {
    uint32_t cmin = rowMap[py], cmax = rowMap[py];
    int end = py + 1;
    while (end < PANEL_H) {
      const uint32_t lo = std::min(cmin, rowMap[end]);
      const uint32_t hi = std::max(cmax, rowMap[end]);
      if (hi - lo >= kTileCols) break;
      cmin = lo;
      cmax = hi;
      ++end;
    }
    const int ncols = cmax - cmin + 1;
    for (int r = 0; r < tileRows; ++r) {
      memcpy(tile + r * kTileCols, canvas + tileSrcRow[r] + cmin, ncols * 2);
    }
    for (; py < end; ++py) {
      uint16_t *d = dst + py * PANEL_W;
      if (py > 0 && rowMap[py] == rowMap[py - 1]) {
        memcpy(d, d - PANEL_W, PANEL_W * 2);
        continue;
      }
      const uint16_t *col = tile + (rowMap[py] - cmin);
      for (int px = 0; px < PANEL_W; ++px) d[px] = col[tileRow[px] * kTileCols];
    }
  }
}

void blackBars(uint16_t *dst) {
  if (rowFirst > 0) memset(dst, 0, rowFirst * PANEL_W * 2);
  if (rowEnd < PANEL_H) memset(dst + rowEnd * PANEL_W, 0, (PANEL_H - rowEnd) * PANEL_W * 2);
  if (colFirst > 0 || colEnd < PANEL_W) {
    for (int py = rowFirst; py < rowEnd; ++py) {
      uint16_t *d = dst + py * PANEL_W;
      memset(d, 0, colFirst * 2);
      memset(d + colEnd, 0, (PANEL_W - colEnd) * 2);
    }
  }
}

void renderFrame() {
  if (rotation & 1) {
    uint16_t *back = display.backBuffer();
    renderRotated(srcPixels, back);
    blackBars(back);
    compositeOverlay(back);
    display.present();
    return;
  }
  const uint16_t *canvas = srcPixels;
  uint16_t *dst = display.backBuffer();
  for (int py = 0; py < PANEL_H; ++py) {
    uint16_t *d = dst + py * PANEL_W;

    if (py > 0 && rowMap[py] == rowMap[py - 1]) {
      memcpy(d, d - PANEL_W, PANEL_W * 2);
      continue;
    }
    const uint16_t *src = canvas + rowMap[py];
    for (int px = 0; px < PANEL_W; ++px) d[px] = src[colMap[px]];
  }
  blackBars(dst);
  compositeOverlay(dst);
  display.present();
}

void closeGif() {
  if (gifOpen) gif.close();
  gifOpen = false;
  free(gifData);
  free(gifCanvas);
  free(gifTurbo);
  gifData = gifCanvas = gifTurbo = nullptr;
  srcPixels = nullptr;
}

bool openGif(int index) {
  closeGif();
  const String &path = gifFiles[index];
  File f = SD_MMC.open(path);
  if (!f) return false;

  const size_t size = f.size();
  if (size == 0 || size > GIF_MAX_BYTES) {
    f.close();
    return false;
  }

  gifData = static_cast<uint8_t *>(ps_malloc(size));
  if (!gifData) {
    f.close();
    return false;
  }
  const size_t read = f.read(gifData, size);
  f.close();
  if (read != size || !gif.open(gifData, size, nullptr)) {
    closeGif();
    return false;
  }
  gifOpen = true;
  gifSrcW = gif.getCanvasWidth();
  gifSrcH = gif.getCanvasHeight();

  const size_t canvasBytes = (size_t)gifSrcW * (gifSrcH + 3) * 3;

  gifTurbo = static_cast<uint8_t *>(allocInternal(TURBO_BUFFER_SIZE + gifSrcW * (gifSrcH + 3)));
  gifCanvas = static_cast<uint8_t *>(allocFast(canvasBytes));
  if (!gifCanvas) {
    closeGif();
    return false;
  }
  memset(gifCanvas, 0, canvasBytes);
  gif.setDrawType(GIF_DRAW_COOKED);
  gif.setFrameBuf(gifCanvas);
  gif.setTurboBuf(gifTurbo);
  srcPixels = reinterpret_cast<const uint16_t *>(gifCanvas + gifSrcW * gifSrcH);
  srcCover = true;
  buildMaps();
  return true;
}

bool sdMounted = false;
bool messagePage = false;
bool helpPage = false;

uint32_t helpLeaveAt = 0;
constexpr int kGifBase = HELP_ENABLED ? 1 : 0;
uint32_t lastRetryAt = 0;
void loadLibrary();

int gifSlots() { return gifFiles.empty() ? 1 : (int)gifFiles.size(); }
int pageCount() { return kGifBase + gifSlots() + (METRO_ENABLED ? 1 : 0); }
bool isHelpIndex(int index) { return HELP_ENABLED && index == 0; }
bool isMetroIndex(int index) { return METRO_ENABLED && index == kGifBase + gifSlots(); }

void setFullSpeed(bool full) {
  static int current = 240;
  const int wanted = full ? 240 : CPU_MHZ_STATIC;
  if (wanted != current) {
    setCpuFrequencyMhz(wanted);
    current = wanted;
  }
}

void setMetroPage(bool on) {
  if (on && !metroPage) metro::enter();
  metroSignature = 0;
  metroPage = on;
}

void showNoGifMessage() {
  if (!sdMounted) showMessage("Pas de carte SD", "Insère une carte microSD");
  else showMessage("Aucun GIF", "Copie des .gif sur la carte SD");
}

void showPage(int index, int dir) {
  showBatteryOverlay(BATTERY_OVERLAY_MS);
  bool reloaded = false;

  for (int tries = 0; tries <= pageCount() + 1; ++tries) {
    const int count = pageCount();
    currentIndex = ((index % count) + count) % count;
    if (isHelpIndex(currentIndex)) {
      closeGif();
      setFullSpeed(false);
      messagePage = false;
      setMetroPage(false);
      helpPage = true;
      vista::drawHelp(ui);
      showUi();
      return;
    }
    helpPage = false;
    if (isMetroIndex(currentIndex)) {
      closeGif();
      setFullSpeed(false);
      messagePage = false;
      setMetroPage(true);
      prefs.putString("last", "#metro");
      return;
    }
    if (gifFiles.empty()) {
      closeGif();
      setFullSpeed(false);
      setMetroPage(false);
      messagePage = true;
      lastRetryAt = millis();
      showNoGifMessage();
      return;
    }
    setFullSpeed(true);
    if (openGif(currentIndex - kGifBase)) {
      messagePage = false;
      setMetroPage(false);
      nextFrameAt = millis();
      prefs.putString("last", gifFiles[currentIndex - kGifBase]);
      return;
    }

    if (!reloaded) {
      reloaded = true;
      loadLibrary();
      if (gifFiles.empty()) {
        index = kGifBase;
        continue;
      }
    }
    index += dir;
  }
  setFullSpeed(false);
  setMetroPage(false);
  messagePage = true;
  lastRetryAt = millis();
  showMessage("GIF illisibles", "Vérifie les fichiers sur la carte SD");
}

void step(int dir) {
  helpLeaveAt = 0;
  if (pageCount() > 0) showPage(currentIndex + dir, dir);
}

int savedIndex() {
  const String last = prefs.getString("last", "");
  if (METRO_ENABLED && last == "#metro") return kGifBase + gifSlots();
  for (size_t i = 0; i < gifFiles.size(); ++i) {
    if (gifFiles[i] == last) return kGifBase + i;
  }
  return kGifBase;
}

bool transferDone = false;

void loadLibrary() {
  sdMounted = mountSd();
  gifFiles.clear();
  if (!sdMounted) return;
  loadGifList();
#if USB_DRIVE
  if (!usbdrive::ready) usbdrive::begin();
#endif
}

void startLibrary() {
  if (HELP_ENABLED && HELP_AT_STARTUP && !gifFiles.empty()) {
    showPage(0, 1);
    helpLeaveAt = (millis() + HELP_STARTUP_MS) | 1;
  } else {
    showPage(savedIndex(), 1);
  }
}

void step(int dir);
void rotateScreen();

void runCommand(const char *line) {
}

void handleSerial() {
  static char line[24];
  static size_t len = 0;
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      line[len] = 0;
      if (len) runCommand(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }
}

void bootScreen() {
  const uint32_t start = millis();
  while (millis() - start < vista::kBootMs) {
    vista::drawBoot(ui, millis() - start);
    showUi();
    handleSerial();
  }
}

void rotateScreen() {
  rotation = (rotation + 1) & 3;
  prefs.putUChar("rot", rotation);
  if (srcPixels) buildMaps();
}

void doublePress() {
  if (metroPage) {
    metro::nextLine();
    prefs.putUChar("dir", metro::lineIndex);
  } else {
    rotateScreen();
  }
}

void handleBootButton() {
  static uint32_t pendingClickAt = 0;
  static bool pending = false;
  ButtonEvent event;
  while (xQueueReceive(bootEvents, &event, 0) == pdTRUE) {
    if (event.heldMs >= LONG_PRESS_MS) {
      pending = false;
      step(-1);
      continue;
    }

    const uint32_t pressedAt = event.releasedAt - event.heldMs;
    if (pending && pressedAt - pendingClickAt <= DOUBLE_CLICK_MS) {
      pending = false;
      doublePress();
      continue;
    }
    if (pending) step(+1);
    pending = true;
    pendingClickAt = event.releasedAt;
  }

  if (pending && !bootDown && millis() - pendingClickAt > DOUBLE_CLICK_MS && uxQueueMessagesWaiting(bootEvents) == 0) {
    pending = false;
    step(+1);
  }
}

bool transferMode = false;
uint32_t transferStartMs = 0;

void enterTransfer() {
  closeGif();
  setFullSpeed(true);
  setMetroPage(false);
  overlayUntil = 0;
  transferStartMs = millis();
  transferMode = true;
  usbdrive::expose(true);
}

void exitTransfer() {
  usbdrive::expose(false);
  transferMode = false;
  transferDone = true;
  delay(200);
  loadLibrary();
  startLibrary();
}

bool handleTransfer() {
#if USB_DRIVE
  static uint32_t hostSince = 0;
  const bool host = usbdrive::hostConnected();
  if (!host) {
    hostSince = 0;
    transferDone = false;
  } else if (!hostSince) {
    hostSince = millis();
  }
  if (transferMode) {
    if (!host || usbdrive::ejected || transferDone) exitTransfer();
    else showMessage("Transfert USB", "Copie tes GIF sur le disque, puis éjecte-le", millis() - transferStartMs);
  } else if (host && !transferDone && sdMounted && usbdrive::ready && millis() - hostSince > 1000) {
    enterTransfer();
  }
#endif
  return transferMode;
}
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);

  Serial.enableReboot(false);

  initBacklight(BACKLIGHT_LEVEL);
  display.init();

  startBootButton();

  ui.pixels = static_cast<uint16_t *>(heap_caps_malloc(kUiW * kUiH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  gif.begin(GIF_PALETTE_RGB565_LE);

  prefs.begin("gifplayer", false);
  rotation = prefs.getUChar("rot", SCREEN_ROTATION) & 3;
  metro::lineIndex = prefs.getUChar("dir", kMetroDefaultLine) % kMetroLineCount;

  for (int i = 0; i < 4; ++i) battery::update(true);
  metro::begin();
  loadLibrary();
#if BOOT_SCREEN
  showBatteryOverlay(vista::kBootMs + BATTERY_OVERLAY_MS);
  bootScreen();
#endif
  startLibrary();
}

void loop() {
  handleSerial();
  if (handleTransfer()) return;

#if BATTERY_OVERLAY
  battery::update();
#endif

  if (overlayShown && (int32_t)(overlayUntil - millis()) <= 0) {
    overlayShown = false;
    if (srcPixels) renderFrame();
  }

  handleBootButton();

  if (helpPage) {
    if (helpLeaveAt && (int32_t)(millis() - helpLeaveAt) >= 0) {
      helpLeaveAt = 0;
      showPage(savedIndex(), 1);
      return;
    }
    delay(20);
    return;
  }

  if (messagePage) {
    if (millis() - lastRetryAt > 3000) {
      lastRetryAt = millis();
      loadLibrary();
      if (!gifFiles.empty()) showPage(kGifBase, 1);
    }
    delay(20);
    return;
  }

#if METRO_ENABLED
  if (metroPage) {
    const uint32_t signature = metro::signature() * 4 + rotation;
    if (signature != metroSignature) {
      metroSignature = signature;
      metro::draw(ui);
      showUi();
    }
    delay(5);
    return;
  }
#endif

  if (gifOpen && (int32_t)(millis() - nextFrameAt) >= 0) {
    int delayMs = 0;
    const int result = gif.playFrame(false, &delayMs);
    if (result < 0) {
      step(+1);
      return;
    }
    renderFrame();
    if (result == 0) gif.reset();

    nextFrameAt += std::max(delayMs, 10);
    if ((int32_t)(millis() - nextFrameAt) > 100) nextFrameAt = millis();
  }

  delay(1);
}
