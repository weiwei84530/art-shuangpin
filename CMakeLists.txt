cmake_minimum_required(VERSION 3.24)
project(mspy_ime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # Sources and string literals are UTF-8 (bopomofo strings in code/tests).
  add_compile_options(/utf-8)
endif()

# --- Engine: vendored McBopomofo conversion stack (MIT), see docs/PROVENANCE.md ---
add_library(mcb_engine STATIC
  engine/gramambular2/reading_grid.cpp
  engine/mcbopomofo/AssociatedPhrasesV2.cpp
  engine/mcbopomofo/ByteBlockBackedDictionary.cpp
  engine/mcbopomofo/McBopomofoLM.cpp
  engine/mcbopomofo/MemoryMappedFile.cpp
  engine/mcbopomofo/ParselessLM.cpp
  engine/mcbopomofo/ParselessPhraseDB.cpp
  engine/mcbopomofo/PhraseReplacementMap.cpp
  engine/mcbopomofo/UserOverrideModel.cpp
  engine/mcbopomofo/UserPhrasesLM.cpp
  engine/mcbopomofo/UTF8Helper.cpp
)
target_include_directories(mcb_engine PUBLIC engine engine/mcbopomofo)

# --- Input core (our code, TSF-free) ---
add_library(mspy_core STATIC
  core/double_pinyin.cpp
  core/syllable_input.cpp
  core/relaxed_tone_lm.cpp
  core/user_preferences.cpp
  core/composer.cpp
)
target_include_directories(mspy_core PUBLIC core)
target_link_libraries(mspy_core PUBLIC mcb_engine)

# --- CLI harness (probe/REPL) ---
add_executable(repl cli/repl.cpp)
target_link_libraries(repl PRIVATE mspy_core)

# --- Typing-drill data generator (web/drills.js) ---
add_executable(drill_gen cli/drill_gen.cpp)
target_link_libraries(drill_gen PRIVATE mspy_core)

# --- Unit tests (vendored upstream tests + ours) ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
# Match the parent project's CRT on MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# MemoryMappedFile has a Win32 branch and a POSIX branch, each with its own
# test file, and the two cannot both be compiled: the upstream POSIX one puts
# TempFile in a named namespace McBopomofo while the Win32 port uses an
# anonymous one, so the suites would collide. Expect two fewer tests off
# Windows -- the POSIX file has 4 cases, the Win32 file 6.
if(WIN32)
  set(MEMORY_MAPPED_FILE_TEST engine/mcbopomofo/MemoryMappedFileTestWin.cpp)
else()
  set(MEMORY_MAPPED_FILE_TEST engine/mcbopomofo/MemoryMappedFileTest.cpp)
endif()

add_executable(engine_tests
  engine/gramambular2/reading_grid_test.cpp
  engine/mcbopomofo/AssociatedPhrasesV2Test.cpp
  engine/mcbopomofo/ByteBlockBackedDictionaryTest.cpp
  engine/mcbopomofo/McBopomofoLMTest.cpp
  ${MEMORY_MAPPED_FILE_TEST}
  engine/mcbopomofo/ParselessLMTest.cpp
  engine/mcbopomofo/ParselessPhraseDBTest.cpp
  engine/mcbopomofo/PhraseReplacementMapTest.cpp
  engine/mcbopomofo/UserOverrideModelTest.cpp
  engine/mcbopomofo/UserPhrasesLMTest.cpp
  engine/mcbopomofo/UTF8HelperTest.cpp
)
target_link_libraries(engine_tests PRIVATE mcb_engine GTest::gtest_main)
gtest_discover_tests(engine_tests)

add_executable(core_tests
  core/double_pinyin_test.cpp
  core/relaxed_tone_lm_test.cpp
  core/user_preferences_test.cpp
  core/composer_test.cpp
)
target_link_libraries(core_tests PRIVATE mspy_core GTest::gtest_main)
gtest_discover_tests(core_tests)
