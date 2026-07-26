// CLI harness for the conversion engine (no TSF involved).
//
// One-shot mode:  repl.exe [--data <path>] [--candidates] <reading> <reading>...
//                 e.g. repl.exe ㄒㄧㄣ ㄎㄨˋ ㄧㄣ ㄕㄨ ㄖㄨˋ ㄈㄚˇ
// Piped mode:     each stdin line = whitespace-separated bopomofo readings
//                 (UTF-8); prints the walked sentence per line.
//
// This is the M1 probe; the M2 interactive REPL (raw key input through the
// double-pinyin input core) will grow on top of this file.

#include <windows.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "McBopomofoLM.h"
#include "gramambular2/reading_grid.h"

namespace {

std::string Narrow(const wchar_t* wide) {
  int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string out(static_cast<size_t>(len) - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
  return out;
}

std::vector<std::string> SplitWhitespace(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) tokens.push_back(tok);
  return tokens;
}

void WalkAndPrint(std::shared_ptr<McBopomofo::McBopomofoLM> lm,
                  const std::vector<std::string>& readings,
                  bool showCandidates) {
  Formosa::Gramambular2::ReadingGrid grid(lm);
  grid.setReadingSeparator("-");
  for (const auto& r : readings) {
    if (!lm->hasUnigrams(r)) {
      std::cout << "[no unigrams for reading: " << r << "]\n";
    }
    grid.insertReading(r);
  }
  auto result = grid.walk();
  std::string sentence;
  for (const auto& v : result.valuesAsStrings()) sentence += v;
  std::cout << sentence << "\n";

  if (showCandidates) {
    for (size_t loc = 0; loc < readings.size(); ++loc) {
      auto candidates = grid.candidatesAt(loc);
      std::cout << "  @" << loc << ":";
      size_t shown = 0;
      for (const auto& c : candidates) {
        std::cout << " " << c.value;
        if (++shown >= 10) break;
      }
      std::cout << "\n";
    }
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string dataPath = "out/data.txt";
  bool showCandidates = false;
  std::vector<std::string> readings;

  for (int i = 1; i < argc; ++i) {
    std::string arg = Narrow(argv[i]);
    if (arg == "--data" && i + 1 < argc) {
      dataPath = Narrow(argv[++i]);
    } else if (arg == "--candidates") {
      showCandidates = true;
    } else {
      readings.push_back(arg);
    }
  }

  auto lm = std::make_shared<McBopomofo::McBopomofoLM>();
  lm->loadLanguageModel(dataPath.c_str());
  if (!lm->isDataModelLoaded()) {
    std::cerr << "failed to load language model: " << dataPath << "\n";
    return 1;
  }

  if (!readings.empty()) {
    WalkAndPrint(lm, readings, showCandidates);
    return 0;
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    auto tokens = SplitWhitespace(line);
    if (tokens.empty()) continue;
    WalkAndPrint(lm, tokens, showCandidates);
  }
  return 0;
}
