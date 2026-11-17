#pragma once

#include <HalFlashPartition.h>

#include <cstddef>
#include <cstdint>

// X3 dual-boot fork: copies of the selected SD .cpfont files in the "fonts"
// data partition. Reading them from flash avoids SD seeks, and a resident copy
// lets optional content sizes load without the heap gate, so the reader's font
// identity no longer depends on free heap at load time (uxjulia/CrossInk#957).
// Without the partition every call reports "not resident" and SD is used.
class FontFlashCache {
 public:
  static constexpr size_t MAX_FILES = 4;     // body + two content sizes, one spare
  static constexpr size_t PATH_BYTES = 128;  // SdCardFont::filePath_ size

  // False when the layout has no fonts partition (stock CrossPoint table).
  bool available() { return begin(); }
  // True when the last store() was for exactly these SD files, unchanged since
  // (size + mtime). Marks their resident copies readable through find().
  bool matches(const char* const* paths, size_t count);
  // Rewrite the area with these files, in order; a file that does not fit is
  // recorded as SD-only. Returns how many are resident. Slow (seconds): call
  // only where the UI shows progress. An SD/flash error leaves no index.
  size_t store(const char* const* paths, size_t count);
  // Location of a copy validated by the latest matches()/store().
  bool find(const char* path, uint32_t& offset, uint32_t& size) const;
  // In the validated set, resident or SD-only. Membership is fixed per set, so
  // load decisions keyed on it do not vary with free heap (#957).
  bool listed(const char* path) const;
  bool read(uint32_t offset, void* bytes, size_t length) const { return flash_.read(offset, bytes, length); }

 private:
  struct Entry {
    char path[PATH_BYTES];
    uint32_t offset;  // 0: recorded but not resident (did not fit)
    uint32_t size;
    uint32_t mtime;
  };
  struct Index {
    uint32_t magic;
    uint32_t count;
    Entry entries[MAX_FILES];
    uint32_t crc;  // uzlib_crc32 of everything before it
  };
  static constexpr uint32_t MAGIC = 0x31434658;  // "XFC1"
  static constexpr uint32_t DATA_START = 0x1000;

  bool begin(bool create = false);  // create: simulator only, makes the backing file
  bool loadIndex();
  enum class Copy { Done, NoRoom, Failed };
  Copy copyFile(const char* path, uint32_t offset, Entry& entry);

  HalFlashPartition flash_{"fonts", 0x40, 0x2a0000};
  Index index_{};
  uint8_t usable_ = 0;    // bit i: index_.entries[i] validated against SD and resident
  bool matched_ = false;  // index_ validated against SD by the latest matches()/store()
  bool indexLoaded_ = false;
};

FontFlashCache& fontFlashCache();
