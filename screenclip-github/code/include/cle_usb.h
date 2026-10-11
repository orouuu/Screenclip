#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <sdmmc_cmd.h>
#include <tusb.h>

namespace usbdrive {
inline USBMSC msc;
inline volatile bool ejected = false;
inline bool ready = false;

struct CardAccess : public fs::SDMMCFS {
  static sdmmc_card_t *card() { return static_cast<CardAccess &>(SD_MMC)._card; }
};

inline int32_t onWrite(uint32_t lba, uint32_t, uint8_t *buffer, uint32_t size) {
  sdmmc_card_t *card = CardAccess::card();
  if (!card) return -1;
  const uint32_t sector = card->csd.sector_size;
  return sdmmc_write_sectors(card, buffer, lba, size / sector) == ESP_OK ? (int32_t)size : -1;
}

inline int32_t onRead(uint32_t lba, uint32_t, void *buffer, uint32_t size) {
  sdmmc_card_t *card = CardAccess::card();
  if (!card) return -1;
  const uint32_t sector = card->csd.sector_size;
  return sdmmc_read_sectors(card, buffer, lba, size / sector) == ESP_OK ? (int32_t)size : -1;
}

inline bool onStartStop(uint8_t, bool start, bool loadEject) {
  if (loadEject && !start) ejected = true;
  return true;
}

inline void begin() {
  msc.vendorID("SCRNCLIP");
  msc.productID("screenclip");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(false);
  ready = msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize());
}

inline bool hostConnected() { return tud_mounted() && !tud_suspended(); }

inline void expose(bool on) {
  if (on) ejected = false;
  msc.mediaPresent(on);
}
}
