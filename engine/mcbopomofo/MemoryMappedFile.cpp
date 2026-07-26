// Copyright (c) 2024 and onwards The McBopomofo Authors.
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#include "MemoryMappedFile.h"

#ifdef _WIN32
// [MspyIME] Win32 port of the POSIX mmap implementation below.

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <string>
#include <utility>

namespace McBopomofo {

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : file_(std::exchange(other.file_, nullptr)),
      mapping_(std::exchange(other.mapping_, nullptr)),
      data_(std::exchange(other.data_, nullptr)),
      length_(std::exchange(other.length_, 0)) {}

MemoryMappedFile& MemoryMappedFile::operator=(
    MemoryMappedFile&& other) noexcept {
  close();
  file_ = std::exchange(other.file_, nullptr);
  mapping_ = std::exchange(other.mapping_, nullptr);
  data_ = std::exchange(other.data_, nullptr);
  length_ = std::exchange(other.length_, 0);
  return *this;
}

MemoryMappedFile::~MemoryMappedFile() { close(); }

bool MemoryMappedFile::open(const char* path) {
  if (file_ != nullptr) {
    return false;
  }

  // Paths are UTF-8 throughout the engine; widen for the W-series API.
  int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
  if (wlen <= 0) {
    return false;
  }
  std::wstring wpath(static_cast<size_t>(wlen), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), wlen);

  HANDLE file =
      CreateFileW(wpath.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  // CreateFileMappingW fails on empty files, matching the mmap behavior.
  LARGE_INTEGER size;
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      static_cast<std::uint64_t>(size.QuadPart) > SIZE_MAX) {
    CloseHandle(file);
    return false;
  }

  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) {
    CloseHandle(file);
    return false;
  }

  void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (data == nullptr) {
    CloseHandle(mapping);
    CloseHandle(file);
    return false;
  }

  file_ = file;
  mapping_ = mapping;
  data_ = data;
  length_ = static_cast<size_t>(size.QuadPart);
  return true;
}

void MemoryMappedFile::close() {
  if (file_ == nullptr) {
    return;
  }
  UnmapViewOfFile(data_);
  CloseHandle(mapping_);
  CloseHandle(file_);
  file_ = nullptr;
  mapping_ = nullptr;
  data_ = nullptr;
  length_ = 0;
}

}  // namespace McBopomofo

#else  // !_WIN32

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

namespace McBopomofo {

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      data_(std::exchange(other.data_, nullptr)),
      length_(std::exchange(other.length_, 0)) {}

MemoryMappedFile& MemoryMappedFile::operator=(
    MemoryMappedFile&& other) noexcept {
  close();
  fd_ = std::exchange(other.fd_, -1);
  data_ = std::exchange(other.data_, nullptr);
  length_ = std::exchange(other.length_, 0);
  return *this;
}

MemoryMappedFile::~MemoryMappedFile() { close(); }

bool MemoryMappedFile::open(const char* path) {
  if (fd_ != -1) {
    return false;
  }

  fd_ = ::open(path, O_RDONLY);
  if (fd_ == -1) {
    return false;
  }

  struct stat sb;
  if (fstat(fd_, &sb) == -1) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  length_ = static_cast<size_t>(sb.st_size);

  // No need to check if length_ is 0; mmmap fails on empty files.
  data_ = mmap(nullptr, length_, PROT_READ, MAP_SHARED, fd_, 0);
  if (data_ == MAP_FAILED) {
    ::close(fd_);
    fd_ = -1;
    length_ = 0;
    data_ = nullptr;
    return false;
  }

  return true;
}

void MemoryMappedFile::close() {
  if (fd_ == -1) {
    return;
  }
  munmap(data_, length_);
  ::close(fd_);
  fd_ = -1;
  length_ = 0;
  data_ = nullptr;
}

}  // namespace McBopomofo

#endif  // _WIN32
