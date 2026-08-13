// Core-only probe: the conversion engine and mspy::Composer, with no
// AppKit and no InputMethodKit anywhere near it.
//
//   artprobe [--data <path>] [--user-choices <path>] --keys "ni3hk3vs 99"
//   artprobe [--data <path>] <bopomofo reading>...
//
// --user-choices loads the same store the shell learns into
// (~/Library/Application Support/ArtShuangpin/user-choices.txt), which is
// what makes the output here identical to the app's.  Since v0.6 those
// records are applied as grid overrides rather than as scores, so loading
// them changes what comes out, not merely how the candidates rank.  The
// file is read only; picks made during a replay live in this process.
//
// It exists to split one question in two.  When something misbehaves on the
// Mac, run this first: if the composer produces the right segments here,
// the bug is in the Objective-C shell, and there is no need to go looking
// in the engine.  It is the local stand-in for the upstream cli/repl.cpp,
// which cannot be built here because it includes <windows.h> and
// vendor/art-shuangpin is a read-only mirror.
//
// Control tokens inside --keys:
//   <  Backspace (also what Tab does)      !  Esc      #  Enter
//   ~  bare-Shift language switch.  Everything after it is fed as English
//      (Composer::feedEnglishChar) until the next ~, so `hk3~ ok~` is the
//      v0.5 「中英切換不上屏」 flow: 好 ␣ok␣ in ONE uncommitted buffer.

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "McBopomofoLM.h"
#include "composer.h"
#include "gramambular2/reading_grid.h"
#include "relaxed_tone_lm.h"
#include "user_preferences.h"

namespace {

const char* StateName(mspy::Composer::State state) {
  switch (state) {
    case mspy::Composer::State::kEmpty:
      return "Empty";
    case mspy::Composer::State::kComposing:
      return "Composing";
    case mspy::Composer::State::kSelecting:
      return "Selecting";
  }
  return "?";
}

void PrintState(mspy::Composer& composer, const std::string& commit) {
  mspy::Composer::DisplaySegments segments = composer.displaySegments();
  std::cout << "  state=" << StateName(composer.state());
  if (!commit.empty()) {
    std::cout << "  commit=\"" << commit << "\"";
  }
  std::cout << "\n  text=" << segments.before << segments.unconfirmed << "|"
            << segments.highlighted << segments.after << "\n";
  std::cout << "  segments: before=\"" << segments.before << "\" unconfirmed=\""
            << segments.unconfirmed << "\" anchor=\"" << segments.highlighted
            << "\" after=\"" << segments.after << "\"\n";
  if (composer.state() == mspy::Composer::State::kSelecting) {
    std::cout << "  page " << (composer.candidatePageIndex() + 1) << "/"
              << composer.candidatePageCount() << ":";
    size_t index = 1;
    for (const auto& candidate : composer.currentPageCandidates()) {
      std::cout << "  " << index++ << "." << candidate.value;
    }
    std::cout << "\n";
  }
}

void FeedKeys(mspy::Composer& composer, const std::string& keys) {
  bool english = false;
  for (char key : keys) {
    std::string label(1, key);
    mspy::Composer::Result result;
    switch (key) {
      case '<':
        label = "Backspace";
        result = composer.feedBackspace();
        break;
      case '!':
        label = "Esc";
        result = composer.feedEsc();
        break;
      case '#':
        label = "Enter";
        result = composer.feedEnter();
        break;
      case '~':
        english = !english;
        label = english ? "Shift->英" : "Shift->中";
        result = composer.switchLanguage(english);
        break;
      default:
        result = english ? composer.feedEnglishChar(key)
                         : composer.feedChar(key);
        break;
    }
    std::cout << "[" << label << "] consumed=" << (result.consumed ? "y" : "n")
              << "\n";
    PrintState(composer, result.commitText);
  }
}

void Walk(const std::shared_ptr<McBopomofo::McBopomofoLM>& lm,
          const std::vector<std::string>& readings) {
  Formosa::Gramambular2::ReadingGrid grid(lm);
  grid.setReadingSeparator("-");
  for (const std::string& reading : readings) {
    if (!lm->hasUnigrams(reading)) {
      std::cout << "[no unigrams for reading: " << reading << "]\n";
    }
    grid.insertReading(reading);
  }
  std::string sentence;
  for (const std::string& value : grid.walk().valuesAsStrings()) {
    sentence += value;
  }
  std::cout << sentence << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string dataPath = "vendor/mspy-data.txt";
  std::string userChoicesPath;
  std::string keys;
  std::vector<std::string> readings;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--data" && i + 1 < argc) {
      dataPath = argv[++i];
    } else if (arg == "--user-choices" && i + 1 < argc) {
      userChoicesPath = argv[++i];
    } else if (arg == "--keys" && i + 1 < argc) {
      keys = argv[++i];
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
  std::cout << "loaded " << dataPath << "\n";

  if (!readings.empty()) {
    Walk(lm, readings);
    return 0;
  }
  if (keys.empty()) {
    std::cerr << "nothing to do; pass --keys \"ni3hk3\" or a list of readings\n";
    return 2;
  }

  // Same stack the shell builds in ArtBridge.mm:
  //   McBopomofoLM -> RelaxedToneLM -> Composer
  // Learning is not a layer of it: the composer holds the store itself and
  // applies it as overrides on the grid (spec §7, 2026-08-09).
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
    std::cout << "loaded " << preferences->size() << " learned records from "
              << userChoicesPath << "\n";
  }

  auto relaxed = std::make_shared<mspy::RelaxedToneLM>(lm);
  mspy::Composer composer(relaxed);
  composer.setPreferences(preferences);
  composer.onLearned = [](const std::string& context,
                          const std::string& reading,
                          const std::string& value) {
    std::cout << "  LEARNED: \"" << value << "\" " << reading << " after "
              << context << "\n";
  };
  FeedKeys(composer, keys);
  return 0;
}
