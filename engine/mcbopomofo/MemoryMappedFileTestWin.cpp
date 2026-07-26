// [MspyIME] Windows replacement for the POSIX-only MemoryMappedFileTest.cpp
// (which relies on mkstemp/mkdtemp). Covers the same behaviors against the
// Win32 port of MemoryMappedFile.

#include "MemoryMappedFile.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "gtest/gtest.h"

namespace McBopomofo {
namespace {

class MemoryMappedFileWinTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("mmap_test_" +
            std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + ::testing::UnitTest::GetInstance()
                      ->current_test_info()
                      ->name());
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::string WriteFile(const std::string& name, const std::string& content) {
    auto path = dir_ / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path.string();
  }

  std::filesystem::path dir_;
};

TEST_F(MemoryMappedFileWinTest, OpenNonexistentFileFails) {
  MemoryMappedFile file;
  EXPECT_FALSE(file.open((dir_ / "no_such_file").string().c_str()));
  EXPECT_FALSE(file.isOpen());
}

TEST_F(MemoryMappedFileWinTest, OpenEmptyFileFails) {
  auto path = WriteFile("empty.txt", "");
  MemoryMappedFile file;
  EXPECT_FALSE(file.open(path.c_str()));
  EXPECT_FALSE(file.isOpen());
}

TEST_F(MemoryMappedFileWinTest, OpenReadsContent) {
  const std::string content = "hello mapped world";
  auto path = WriteFile("data.txt", content);

  MemoryMappedFile file;
  ASSERT_TRUE(file.open(path.c_str()));
  EXPECT_TRUE(file.isOpen());
  ASSERT_EQ(file.length(), content.size());
  EXPECT_EQ(std::string(file.data(), file.length()), content);

  file.close();
  EXPECT_FALSE(file.isOpen());
  EXPECT_EQ(file.length(), 0u);
}

TEST_F(MemoryMappedFileWinTest, DoubleOpenFails) {
  auto path = WriteFile("data.txt", "content");
  MemoryMappedFile file;
  ASSERT_TRUE(file.open(path.c_str()));
  EXPECT_FALSE(file.open(path.c_str()));
  EXPECT_TRUE(file.isOpen());
}

TEST_F(MemoryMappedFileWinTest, MoveTransfersOwnership) {
  const std::string content = "movable";
  auto path = WriteFile("data.txt", content);

  MemoryMappedFile a;
  ASSERT_TRUE(a.open(path.c_str()));

  MemoryMappedFile b(std::move(a));
  EXPECT_FALSE(a.isOpen());
  ASSERT_TRUE(b.isOpen());
  EXPECT_EQ(std::string(b.data(), b.length()), content);

  MemoryMappedFile c;
  c = std::move(b);
  EXPECT_FALSE(b.isOpen());
  ASSERT_TRUE(c.isOpen());
  EXPECT_EQ(std::string(c.data(), c.length()), content);
}

TEST_F(MemoryMappedFileWinTest, Utf8PathSupported) {
  const std::string content = "utf8 path";
  auto path = dir_ / L"詞庫測試.txt";
  std::ofstream out(path, std::ios::binary);
  out << content;
  out.close();

  // MemoryMappedFile::open takes UTF-8 encoded paths.
  auto u8 = path.u8string();
  MemoryMappedFile file;
  ASSERT_TRUE(file.open(reinterpret_cast<const char*>(u8.c_str())));
  EXPECT_EQ(std::string(file.data(), file.length()), content);
}

}  // namespace
}  // namespace McBopomofo
