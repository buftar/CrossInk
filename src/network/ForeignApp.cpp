#include "ForeignApp.h"

#include <Logging.h>
#include <Preferences.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <string.h>

namespace ota_boot {

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
  String nvsName = otaPrefs.getString(key, "");
  otaPrefs.end();

  if (!nvsName.isEmpty()) {
    LOG_DBG("BOOT", "Foreign app detected in OTA slot %d: \"%s\" (from ota_names)", slot, nvsName.c_str());
    return std::string(nvsName.c_str());
  }

  // Fallback to project_name from app descriptor
  LOG_DBG("BOOT", "Foreign app detected in OTA slot %d: \"%s\" (from app descriptor)", slot, targetDesc.project_name);
  return std::string(targetDesc.project_name);
}

}  // namespace ota_boot
