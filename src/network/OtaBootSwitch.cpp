#include "OtaBootSwitch.h"

#include <Logging.h>
#include <Preferences.h>
#include <esp_app_format.h>
#include <esp_rom_crc.h>
#include <esp_ota_ops.h>
#include <spi_flash_mmap.h>
#include <string.h>

namespace ota_boot {

uint32_t computeSeqCrc(uint32_t seq) {
  return esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), kOtaSeqCrcLen);
}

bool switchTo(const esp_partition_t* dest) {
  if (!dest) return false;

  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    LOG_ERR("BOOT", "otadata partition not found");
    return false;
  }
  if (otadata->size < 2 * SPI_FLASH_SEC_SIZE) {
    LOG_ERR("BOOT", "otadata too small: %u", static_cast<unsigned>(otadata->size));
    return false;
  }

  SelectEntry slots[2] = {};
  if (esp_partition_read(otadata, 0, &slots[0], sizeof(SelectEntry)) != ESP_OK ||
      esp_partition_read(otadata, SPI_FLASH_SEC_SIZE, &slots[1], sizeof(SelectEntry)) != ESP_OK) {
    LOG_ERR("BOOT", "otadata read failed");
    return false;
  }

  // Pick the slot with valid CRC and highest seq, ignoring INVALID/ABORTED.
  int activeIdx = -1;
  uint32_t activeSeq = 0;
  for (int i = 0; i < 2; ++i) {
    if (slots[i].ota_seq == 0xFFFFFFFFu) continue;
    if (slots[i].crc != computeSeqCrc(slots[i].ota_seq)) continue;
    if (slots[i].ota_state == kOtaImgInvalid || slots[i].ota_state == kOtaImgAborted) continue;
    if (activeIdx < 0 || slots[i].ota_seq > activeSeq) {
      activeIdx = i;
      activeSeq = slots[i].ota_seq;
    }
  }
  LOG_INF("BOOT", "otadata: active slot=%d seq=%u", activeIdx, static_cast<unsigned>(activeSeq));

  // ota_seq encoding: (seq - 1) % NUM_OTA_PARTITIONS picks the partition.
  const uint32_t destOtaIdx =
      static_cast<uint32_t>(dest->subtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  if (destOtaIdx > 15) {
    LOG_ERR("BOOT", "dest is not an OTA app partition (subtype=0x%02X)", dest->subtype);
    return false;
  }

  // Find smallest seq > activeSeq such that (seq-1) % 2 == destOtaIdx,
  // assuming 2 OTA partitions (matches our partitions.csv with ota_0 + ota_1).
  uint32_t newSeq = activeSeq + 1;
  while (((newSeq - 1u) % 2u) != (destOtaIdx % 2u)) ++newSeq;

  SelectEntry next = {};
  next.ota_seq = newSeq;
  memset(next.seq_label, 0xFF, sizeof(next.seq_label));
  next.ota_state = kOtaImgNew;
  next.crc = computeSeqCrc(next.ota_seq);

  // Write to the OTHER slot (so the bootloader sees a higher seq there).
  // X3 otadata partition is 512 bytes with two SelectEntry at offsets 0 and 32.
  // (SPI_FLASH_SEC_SIZE is 4096 — too large for this partition.)
  const int targetSlot = (activeIdx == 0) ? 1 : 0;
  const size_t targetOff = static_cast<size_t>(targetSlot) * sizeof(SelectEntry);

  // Erase the full partition (512 bytes) since we can't erase a single entry.
  // esp_partition_erase_range requires sector-aligned addresses; the whole
  // otadata partition is one erase unit on the X3.
  if (esp_partition_erase_range(otadata, 0, otadata->size) != ESP_OK) {
    LOG_ERR("BOOT", "otadata erase failed");
    return false;
  }

  // Rewrite BOTH slots: the active one (restored) and the target (new).
  // After a full erase both slots are 0xFF, so we must write both.
  for (int i = 0; i < 2; ++i) {
    size_t off = static_cast<size_t>(i) * sizeof(SelectEntry);
    if (i == targetSlot) {
      if (esp_partition_write(otadata, off, &next, sizeof(next)) != ESP_OK) {
        LOG_ERR("BOOT", "otadata write failed (slot=%d)", targetSlot);
        return false;
      }
    } else {
      // Restore the active slot
      if (esp_partition_write(otadata, off, &slots[i], sizeof(SelectEntry)) != ESP_OK) {
        LOG_ERR("BOOT", "otadata write failed (restore slot=%d)", i);
        return false;
      }
    }
  }

  LOG_INF("BOOT", "otadata: wrote slot=%d seq=%u crc=0x%08x -> %s", targetSlot, static_cast<unsigned>(newSeq),
          static_cast<unsigned>(next.crc), dest->label);
  return true;
}

// ============================================================================
// Foreign-app detection — zero heap when slot is empty or holds own app
// ============================================================================
std::string getForeignAppName(const esp_partition_t* target) {
  if (!target) return {};

  // Read our own app descriptor to get our identity
  const esp_app_desc_t* selfDesc = esp_ota_get_app_description();
  if (!selfDesc) return {};

  // Read the target partition's app descriptor
  esp_app_desc_t targetDesc;
  if (esp_ota_get_partition_description(target, &targetDesc) != ESP_OK) return {};

  // If project names match, it's our own app (or empty/uninitialized)
  if (strncmp(selfDesc->project_name, targetDesc.project_name, sizeof(targetDesc.project_name)) == 0) {
    return {};
  }

  // Different app — look up the display name from ota_names NVS
  int slot = target->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
  char key[8];
  snprintf(key, sizeof(key), "ota_%d", slot);

  Preferences otaPrefs;
  otaPrefs.begin("ota_names", true);  // read-only
  String nvsString = otaPrefs.getString(key, "");
  otaPrefs.end();
  const char* nvsName = nvsString.c_str();

  if (nvsName && nvsName[0] != '\0') {
    LOG_DBG("BOOT", "Foreign app detected in OTA slot %d: \"%s\" (from ota_names)", slot, nvsName);
    return std::string(nvsName);
  }

  // Fallback to project_name from app descriptor
  LOG_DBG("BOOT", "Foreign app detected in OTA slot %d: \"%s\" (from app descriptor)", slot,
          targetDesc.project_name);
  return std::string(targetDesc.project_name);
}

}  // namespace ota_boot
