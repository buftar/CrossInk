set(FONT_FLASH_SOURCES ../../lib/EpdFont/FontFlashCache.cpp ../../lib/HalFlashPartition/HalFlashPartition.cpp
  ../../lib/uzlib/src/uzlib_checksums.c)
set_source_files_properties(../../lib/HalFlashPartition/HalFlashPartition.cpp PROPERTIES COMPILE_DEFINITIONS SIMULATOR)

add_executable(sd_card_font_intervals SdCardFontIntervalsTest.cpp
  ../../lib/EpdFont/SdCardFont.cpp ../../lib/EpdFont/SdCardFontManager.cpp
  ../../lib/EpdFont/EpdFontFamily.cpp ../../lib/EpdFont/EpdFont.cpp ../../lib/Utf8/Utf8.cpp ${FONT_FLASH_SOURCES})
target_compile_features(sd_card_font_intervals PRIVATE cxx_std_20)
target_include_directories(sd_card_font_intervals PRIVATE stubs ../../src ../../lib/EpdFont ../../lib/Memory
  ../../lib/MemoryBudget ../../lib/Utf8 ../../lib/HalFlashPartition ../../lib/uzlib/src)
target_compile_options(sd_card_font_intervals PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(sd_card_font_intervals PRIVATE -fsanitize=address,undefined)
add_test(NAME sd_card_font_intervals COMMAND sd_card_font_intervals)

# X3 dual-boot fork: flash copies of SD fonts (lib/EpdFont/FontFlashCache).
add_executable(font_flash_cache FontFlashCacheTest.cpp ${FONT_FLASH_SOURCES})
target_compile_features(font_flash_cache PRIVATE cxx_std_20)
target_include_directories(font_flash_cache PRIVATE stubs ../../lib/EpdFont ../../lib/Memory ../../lib/MemoryBudget
  ../../lib/HalFlashPartition ../../lib/uzlib/src)
target_compile_options(font_flash_cache PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(font_flash_cache PRIVATE -fsanitize=address,undefined)
add_test(NAME font_flash_cache COMMAND font_flash_cache)
