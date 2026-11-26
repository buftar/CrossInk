#include <ForeignApp.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <gtest/gtest.h>

#include <cstring>

// The SD-card and Wi-Fi update guards and the Home "Switch to" entry all rely on this lookup.
namespace {
const esp_partition_t ota0{ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x800000, "app0"};
const esp_partition_t ota1{ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + 1, 0x500000, "app1"};
esp_app_desc_t self{};
esp_app_desc_t other{};
bool otherValid;  // false = erased or corrupt slot

void setName(esp_app_desc_t& desc, const char* name) { std::strncpy(desc.project_name, name, sizeof(desc.project_name)); }
}  // namespace

const esp_app_desc_t* esp_ota_get_app_description() { return &self; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t*, esp_app_desc_t* desc) {
  if (!otherValid) return -1;
  *desc = other;
  return ESP_OK;
}

class ForeignApp : public testing::Test {
 protected:
  void SetUp() override {
    setName(self, "CrossInk");
    setName(other, "xteink-writer-firmware");
    otherValid = true;
    fakeNvs.clear();
    fakeNvsOpened.clear();
  }
};

TEST_F(ForeignApp, NoTargetPartition) { EXPECT_EQ(ota_boot::getForeignAppName(nullptr), ""); }

TEST_F(ForeignApp, EmptyOrCorruptSlotIsNotForeign) {
  otherValid = false;
  fakeNvs["ota_names/ota_1"] = "MicroSlate";  // a stale name must not resurrect an erased slot
  EXPECT_EQ(ota_boot::getForeignAppName(&ota1), "");
}

TEST_F(ForeignApp, OwnAppInOtherSlotIsNotForeign) {
  setName(other, "CrossInk");
  EXPECT_EQ(ota_boot::getForeignAppName(&ota1), "");
}

TEST_F(ForeignApp, ForeignAppUsesItsRegisteredName) {
  fakeNvs["ota_names/ota_1"] = "MicroSlate";
  EXPECT_EQ(ota_boot::getForeignAppName(&ota1), "MicroSlate");
  EXPECT_EQ(fakeNvsOpened, "ota_names");
}

TEST_F(ForeignApp, ForeignAppWithoutRegisteredNameFallsBackToProjectName) {
  EXPECT_EQ(ota_boot::getForeignAppName(&ota1), "xteink-writer-firmware");
}

TEST_F(ForeignApp, NameIsLookedUpForTheTargetSlot) {
  fakeNvs["ota_names/ota_1"] = "MicroSlate";
  fakeNvs["ota_names/ota_0"] = "Other";
  EXPECT_EQ(ota_boot::getForeignAppName(&ota0), "Other");
}
