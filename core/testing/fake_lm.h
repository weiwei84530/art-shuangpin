// Minimal in-memory LanguageModel for unit tests.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "gramambular2/language_model.h"

namespace mspy::testing {

class FakeLM : public Formosa::Gramambular2::LanguageModel {
 public:
  void add(const std::string& key, const std::string& value, double score) {
    entries_[key].emplace_back(value, score);
  }

  std::vector<Unigram> getUnigrams(const std::string& key) override {
    auto it = entries_.find(key);
    return it == entries_.end() ? std::vector<Unigram>{} : it->second;
  }

  bool hasUnigrams(const std::string& key) override {
    return entries_.count(key) != 0;
  }

 private:
  std::unordered_map<std::string, std::vector<Unigram>> entries_;
};

}  // namespace mspy::testing
