// X3 dual-boot fork: FontFlashCache stores SD font copies in a flash partition
// (a host file here) and must never serve a copy that no longer matches SD.
#include <FontFlashCache.h>
#include <HalStorage.h>  // test stub: fileModificationTime
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> writeFile(const char* path, const size_t size, const uint8_t seed) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(seed + i * 31);
  FILE* f = std::fopen(path, "wb");
  assert(f && std::fwrite(bytes.data(), 1, size, f) == size);
  std::fclose(f);
  return bytes;
}

static bool flashEquals(const FontFlashCache& cache, const char* path, const std::vector<uint8_t>& expected) {
  uint32_t offset = 0, size = 0;
  if (!cache.find(path, offset, size) || size != expected.size()) return false;
  std::vector<uint8_t> got(size);
  // Odd-sized reads at an odd offset, like SdCardFont's record reads.
  for (uint32_t at = 0; at < size; at += 333) {
    const uint32_t n = std::min<uint32_t>(333, size - at);
    if (!cache.read(offset + at, got.data() + at, n)) return false;
  }
  return got == expected;
}

int main() {
  char dir[] = "/tmp/font_flash_cache_XXXXXX";
  assert(mkdtemp(dir) && chdir(dir) == 0);
  const char* a = "a.cpfont";
  const char* b = "b.cpfont";
  const auto bytesA = writeFile(a, 10000, 1);
  const auto bytesB = writeFile(b, 5000, 7);
  const char* both[] = {a, b};

  {
    FontFlashCache cache;
    uint32_t offset, size;
    assert(!cache.matches(both, 2));  // erased area: nothing resident
    assert(!cache.find(a, offset, size));
    assert(cache.store(both, 2) == 2);
    assert(flashEquals(cache, a, bytesA) && flashEquals(cache, b, bytesB));
    const char* other[] = {b, a};
    assert(!cache.matches(other, 2));  // a different request is not a match
    assert(!cache.find(a, offset, size));
  }
  {
    FontFlashCache rebooted;  // the index survives a reboot
    assert(rebooted.matches(both, 2));
    assert(flashEquals(rebooted, a, bytesA) && flashEquals(rebooted, b, bytesB));
    fileModificationTime = 42;  // file replaced on SD: never serve the old copy
    uint32_t offset, size;
    assert(!rebooted.matches(both, 2));
    assert(!rebooted.find(a, offset, size));
  }
  {
    // A file larger than the area is recorded as SD-only, so the set still
    // matches afterwards (no recopy on every book open).
    const char* big = "big.cpfont";
    writeFile(big, 0x2a0000, 3);
    const char* withBig[] = {a, big};
    FontFlashCache cache;
    assert(cache.store(withBig, 2) == 1);
    uint32_t offset, size;
    assert(cache.matches(withBig, 2));
    assert(flashEquals(cache, a, bytesA) && !cache.find(big, offset, size));
    assert(cache.listed(big) && cache.listed(a) && !cache.listed(b));  // SD-only still keys load decisions
  }
  {
    // An unreadable file aborts the store and leaves no index.
    const char* missing[] = {a, "missing.cpfont"};
    FontFlashCache cache;
    assert(cache.store(missing, 2) == 0);
    FontFlashCache rebooted;
    assert(!rebooted.matches(both, 2) && !rebooted.matches(missing, 2));
  }
  std::printf("font_flash_cache: ok\n");
  return 0;
}
