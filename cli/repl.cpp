// CLI harness for the conversion engine and input core (no TSF involved).
//
// Reading mode:   repl.exe [--data <path>] [--candidates] <reading>...
//                 e.g. repl.exe ㄒㄧㄣ ㄎㄨˋ ㄧㄣ ㄕㄨ ㄖㄨˋ ㄈㄚˇ
// Key mode:       repl.exe --keys "ni3hk3."
//                 feeds raw double-pinyin keys through the Composer and
//                 prints every state transition. Digits are live controls
//                 (8 opens the menu, 9/0 move the cursor, 1-6 select,
//                 7/8 page). Control tokens:
//                   <  Backspace                !  Esc
//                   #  bare Shift tap (中/英)    \n Enter
// Piped mode:     each stdin line = whitespace-separated bopomofo readings.

#include <windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "McBopomofoLM.h"
#include "composer.h"
#include "gramambular2/reading_grid.h"
#include "relaxed_tone_lm.h"
#include "user_preferences.h"

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

const char* StateName(mspy::Composer::State s) {
  switch (s) {
    case mspy::Composer::State::kEmpty: return "Empty";
    case mspy::Composer::State::kComposing: return "Composing";
    case mspy::Composer::State::kSelecting: return "Selecting";
  }
  return "?";
}

void PrintComposerState(const mspy::Composer& composer,
                        const mspy::Composer::Result& result, char key) {
  std::cout << "  '" << key << "' -> [" << StateName(composer.state())
            << "] \"" << composer.composedText() << "\"";
  auto segments = composer.displaySegments();
  if (!segments.highlighted.empty())
    std::cout << " anchor:[" << segments.highlighted << "]";
  if (!result.consumed) std::cout << " (pass-through)";
  if (!result.commitText.empty())
    std::cout << " COMMIT:\"" << result.commitText << "\"";
  if (composer.state() == mspy::Composer::State::kSelecting) {
    std::cout << " candidates(page " << (composer.candidatePageIndex() + 1)
              << "/" << composer.candidatePageCount() << "):";
    size_t shown = 0;
    for (const auto& c : composer.currentPageCandidates()) {
      std::cout << " " << (shown + 1) << "." << c.value;
      ++shown;
    }
  }
  std::cout << "\n";
}

void RunKeyMode(std::shared_ptr<McBopomofo::McBopomofoLM> lm,
                std::shared_ptr<mspy::UserPreferences> preferences,
                const std::string& keys) {
  auto relaxed = std::make_shared<mspy::RelaxedToneLM>(lm);
  mspy::Composer composer(relaxed);
  composer.setPreferences(preferences);
  // Echo what the shell would learn, so a key sequence shows its own
  // effect on the store.
  composer.onLearned = [](const std::string& context,
                          const std::string& reading,
                          const std::string& value) {
    std::cout << "  LEARNED: \"" << value << "\" " << reading << " after "
              << context << "\n";
  };
  // '#' stands for the bare Shift tap, so a key sequence can cross the
  // Chinese/English boundary the way the shell does.
  bool english = false;
  for (char c : keys) {
    mspy::Composer::Result r;
    switch (c) {
      case '<': r = composer.feedBackspace(); break;
      case '!': r = composer.feedEsc(); break;
      case '\n': r = composer.feedEnter(); break;
      case '#':
        english = !english;
        r = composer.switchLanguage(english);
        break;
      default:
        r = english ? composer.feedEnglishChar(c) : composer.feedChar(c);
        break;
    }
    PrintComposerState(composer, r, c);
  }
  auto final = composer.feedEnter();
  if (!final.commitText.empty())
    std::cout << "  FINAL COMMIT: \"" << final.commitText << "\"\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);

  std::string dataPath = "out/data.txt";
  std::string userChoicesPath;
  std::string keySequence;
  bool keyMode = false;
  bool showCandidates = false;
  std::vector<std::string> readings;

  for (int i = 1; i < argc; ++i) {
    std::string arg = Narrow(argv[i]);
    if (arg == "--data" && i + 1 < argc) {
      dataPath = Narrow(argv[++i]);
    } else if (arg == "--user-choices" && i + 1 < argc) {
      userChoicesPath = Narrow(argv[++i]);
    } else if (arg == "--keys" && i + 1 < argc) {
      keyMode = true;
      keySequence = Narrow(argv[++i]);
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

  // Same file the shell learns into (%APPDATA%\MspyIME\user-choices.txt);
  // loading it here reproduces the shell's corrections exactly.
  auto preferences = std::make_shared<mspy::UserPreferences>();
  if (!userChoicesPath.empty()) {
    std::ifstream in(userChoicesPath, std::ios::binary);
    if (!in.is_open()) {
      std::cerr << "failed to open user choices: " << userChoicesPath << "\n";
      return 1;
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    preferences->loadFromText(text);
  }

  if (keyMode) {
    RunKeyMode(lm, preferences, keySequence);
    return 0;
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
