#include "FontFlashCache.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <uzlib.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>

FontFlashCache& fontFlashCache() {
  static FontFlashCache cache;
  return cache;
}

bool FontFlashCache::begin(const bool create) {
  if (!flash_.begin(create)) return false;
  if (!indexLoaded_) {
    loadIndex();  // invalid index = nothing resident
    indexLoaded_ = true;
  }
  return true;
}

bool FontFlashCache::loadIndex() {
  index_ = {};
  Index stored{};
  if (!flash_.read(0, &stored, sizeof(stored))) return false;
  if (stored.magic != MAGIC || stored.count > MAX_FILES ||
      stored.crc != uzlib_crc32(&stored, offsetof(Index, crc), 0xffffffff)) {
    return false;  // erased, torn or foreign: nothing resident
  }
  index_ = stored;
  return true;
}

static bool sdStat(const char* path, uint32_t& size, uint32_t& mtime) {
  HalFile file;
  if (!Storage.openFileForRead("FFC", path, file)) return false;
  size = file.fileSize();
  mtime = file.modificationTime();
  file.close();
  return true;
}

bool FontFlashCache::matches(const char* const* paths, const size_t count) {
  usable_ = 0;
  matched_ = false;
  if (!begin() || count == 0 || count != index_.count) return false;
  uint8_t valid = 0;
  for (size_t i = 0; i < count; ++i) {
    const Entry& entry = index_.entries[i];
    uint32_t size = 0, mtime = 0;
    if (std::strncmp(entry.path, paths[i], PATH_BYTES) != 0 || !sdStat(paths[i], size, mtime) || size != entry.size ||
        mtime != entry.mtime)
      return false;
    if (entry.offset) valid |= static_cast<uint8_t>(1u << i);
  }
  usable_ = valid;
  matched_ = true;
  return true;
}

FontFlashCache::Copy FontFlashCache::copyFile(const char* path, const uint32_t offset, Entry& entry) {
  HalFile file;
  if (!Storage.openFileForRead("FFC", path, file)) return Copy::Failed;
  entry = {};
  std::strncpy(entry.path, path, PATH_BYTES - 1);
  entry.offset = offset;
  entry.size = file.fileSize();
  entry.mtime = file.modificationTime();
  const uint32_t span = (entry.size + 4095u) & ~4095u;
  if (std::strlen(path) >= PATH_BYTES || entry.size == 0 || offset > flash_.size() || span > flash_.size() - offset) {
    LOG_INF("FFC", "Not cached (does not fit), stays on SD: %s", path);
    entry.offset = 0;
    file.close();
    return Copy::NoRoom;
  }
  // 4 KB matches the flash sector; transient, freed before the font loads.
  auto buffer = makeUniqueNoThrow<uint8_t[]>(4096);
  if (!buffer) LOG_ERR("FFC", "No buffer to copy %s", path);
  bool ok = buffer && flash_.erase(offset, span);
  for (uint32_t done = 0; ok && done < entry.size;) {
    const uint32_t n = std::min<uint32_t>(4096, entry.size - done);
    ok = file.read(buffer.get(), n) == static_cast<int>(n) && flash_.write(offset + done, buffer.get(), n);
    done += n;
#if defined(ARDUINO)
    delay(1);  // let the idle task run during a multi-second copy (task watchdog), as AssetCacheStorage does
#endif
  }
  file.close();
  if (!ok) LOG_ERR("FFC", "Copy failed: %s", path);
  return ok ? Copy::Done : Copy::Failed;
}

size_t FontFlashCache::store(const char* const* paths, const size_t count) {
  usable_ = 0;
  matched_ = false;
  if (!begin(true)) return 0;
  index_ = {};
  // Invalidate first: a copy interrupted by power loss leaves no index.
  if (!flash_.erase(0, DATA_START)) {
    LOG_ERR("FFC", "Cannot erase font cache index");
    return 0;
  }
  if (count == 0 || count > MAX_FILES) return 0;
  uint32_t offset = DATA_START;
  size_t resident = 0;
  for (size_t i = 0; i < count; ++i) {
    Entry& entry = index_.entries[i];
    const Copy result = copyFile(paths[i], offset, entry);
    if (result == Copy::Failed) {
      index_ = {};
      return 0;  // index stays erased; the next store() retries
    }
    if (result == Copy::Done) {
      usable_ |= static_cast<uint8_t>(1u << i);
      offset += (entry.size + 4095u) & ~4095u;
      ++resident;
    }
  }
  index_.magic = MAGIC;
  index_.count = static_cast<uint32_t>(count);
  index_.crc = uzlib_crc32(&index_, offsetof(Index, crc), 0xffffffff);
  if (!flash_.write(0, &index_, sizeof(index_))) {
    LOG_ERR("FFC", "Cannot write font cache index");
    index_ = {};
    usable_ = 0;
    return 0;
  }
  matched_ = true;
  LOG_INF("FFC", "Cached %u of %u font files (%u bytes)", unsigned(resident), unsigned(count),
          unsigned(offset - DATA_START));
  return resident;
}

bool FontFlashCache::find(const char* path, uint32_t& offset, uint32_t& size) const {
  for (uint32_t e = 0; e < index_.count; ++e) {
    if ((usable_ & (1u << e)) == 0 || index_.entries[e].offset == 0 ||
        std::strncmp(index_.entries[e].path, path, PATH_BYTES) != 0)
      continue;
    offset = index_.entries[e].offset;
    size = index_.entries[e].size;
    return true;
  }
  return false;
}

bool FontFlashCache::listed(const char* path) const {
  for (uint32_t e = 0; matched_ && e < index_.count; ++e)
    if (std::strncmp(index_.entries[e].path, path, PATH_BYTES) == 0) return true;
  return false;
}
