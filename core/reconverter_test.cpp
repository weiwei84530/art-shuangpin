#include "reconverter.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "testing/fake_lm.h"

namespace mspy {
namespace {

class ReconverterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    lm_ = std::make_shared<testing::FakeLM>();
    // 行 is a polyphone: ㄒㄧㄥˊ and ㄏㄤˊ. 型 exists under both readings
    // (contrived) to exercise value dedup keeping the best score.
    lm_->add("ㄒㄧㄥˊ", "行", -3);
    lm_->add("ㄒㄧㄥˊ", "形", -3.5);
    lm_->add("ㄒㄧㄥˊ", "型", -4);
    lm_->add("ㄏㄤˊ", "行", -4.2);
    lm_->add("ㄏㄤˊ", "航", -4.5);
    lm_->add("ㄏㄤˊ", "杭", -5);
    lm_->add("ㄏㄤˊ", "型", -5.5);
    reverse_["行"] = {{"ㄒㄧㄥˊ", -3}, {"ㄏㄤˊ", -4.2}};

    lm_->add("ㄊㄧㄢ", "天", -3);
    lm_->add("ㄊㄧㄢ", "添", -5);
    reverse_["天"] = {{"ㄊㄧㄢ", -3}};
    lm_->add("ㄐㄧㄣ", "今", -3.1);
    lm_->add("ㄐㄧㄣ", "金", -3.2);
    lm_->add("ㄐㄧㄣ", "津", -4);
    reverse_["今"] = {{"ㄐㄧㄣ", -3.1}};
    reverse_["津"] = {{"ㄐㄧㄣ", -4}};
    lm_->add("ㄐㄧㄣ-ㄊㄧㄢ", "今天", -2);
    lm_->add("ㄐㄧㄣ-ㄊㄧㄢ", "金天", -6);
    reverse_["今天"] = {{"ㄐㄧㄣ-ㄊㄧㄢ", -2}};

    // Eight homophones of 多 to exercise paging (page size 6).
    lm_->add("ㄉㄨㄛ", "多", -3);
    const char* kDuo[] = {"剁", "朵", "躲", "舵", "惰", "墮", "跺", "鐸"};
    double score = -4;
    for (const char* value : kDuo) lm_->add("ㄉㄨㄛ", value, score -= 0.1);
    reverse_["多"] = {{"ㄉㄨㄛ", -3}};

    // 獨 is its own only homophone: the self filter empties the menu.
    lm_->add("ㄉㄨˊ", "獨", -4);
    reverse_["獨"] = {{"ㄉㄨˊ", -4}};

    recon_ = std::make_unique<Reconverter>(
        lm_, [this](const std::string& value) {
          auto it = reverse_.find(value);
          return it == reverse_.end()
                     ? std::vector<Reconverter::FoundReading>{}
                     : it->second;
        });
  }

  std::shared_ptr<testing::FakeLM> lm_;
  std::map<std::string, std::vector<Reconverter::FoundReading>> reverse_;
  std::unique_ptr<Reconverter> recon_;
};

TEST_F(ReconverterTest, PolyphoneUnionsAllReadings) {
  ASSERT_TRUE(recon_->start({"行"}));
  // Union over both readings, self filtered, dedup keeps 型's best score
  // (-4): 形(-3.5), 型(-4), 航(-4.5), 杭(-5).
  const auto& c = recon_->candidates();
  ASSERT_EQ(c.size(), 4);
  EXPECT_EQ(c[0].value, "形");
  EXPECT_EQ(c[1].value, "型");
  EXPECT_EQ(c[2].value, "航");
  EXPECT_EQ(c[3].value, "杭");
  for (const auto& candidate : c) EXPECT_EQ(candidate.spanLength, 1);
}

TEST_F(ReconverterTest, TwoCharSpanListsWordCandidatesFirst) {
  ASSERT_TRUE(recon_->start({"今", "天"}));
  const auto& c = recon_->candidates();
  // Span 2 (金天; 今天 is the span text itself, filtered) before span 1
  // (anchor 天: 添).
  ASSERT_EQ(c.size(), 2);
  EXPECT_EQ(c[0].value, "金天");
  EXPECT_EQ(c[0].spanLength, 2);
  EXPECT_EQ(c[1].value, "添");
  EXPECT_EQ(c[1].spanLength, 1);
}

TEST_F(ReconverterTest, FallbackJoinsPerCharTopReadings) {
  // 津天 is not a dictionary word (no reverse entry), but joining the
  // per-char top readings gives ㄐㄧㄣ-ㄊㄧㄢ whose words all qualify.
  ASSERT_TRUE(recon_->start({"津", "天"}));
  const auto& c = recon_->candidates();
  ASSERT_EQ(c.size(), 3);
  EXPECT_EQ(c[0].value, "今天");
  EXPECT_EQ(c[0].spanLength, 2);
  EXPECT_EQ(c[1].value, "金天");
  EXPECT_EQ(c[1].spanLength, 2);
  // Anchor span 1 (text 天, filtered as self): only 添 remains.
  EXPECT_EQ(c[2].value, "添");
  EXPECT_EQ(c[2].spanLength, 1);
}

TEST_F(ReconverterTest, PagingAndSelection) {
  ASSERT_TRUE(recon_->start({"多"}));  // 8 candidates -> 2 pages
  EXPECT_EQ(recon_->pageCount(), 2);
  EXPECT_EQ(recon_->currentPageCandidates().size(), 6);

  auto r = recon_->feedKey('7');  // previous page at the first page: no-op
  EXPECT_EQ(r.action, Reconverter::Action::kNone);
  r = recon_->feedKey('8');
  EXPECT_EQ(r.action, Reconverter::Action::kPageChanged);
  EXPECT_EQ(recon_->pageIndex(), 1);
  EXPECT_EQ(recon_->currentPageCandidates().size(), 2);

  r = recon_->feedKey('8');  // next page at the last page: no-op
  EXPECT_EQ(r.action, Reconverter::Action::kNone);
  r = recon_->feedKey('3');  // page slot 3 is empty on the last page
  EXPECT_EQ(r.action, Reconverter::Action::kNone);
  EXPECT_TRUE(recon_->active());

  r = recon_->feedKey('2');  // second entry of page 2 = candidates()[7]
  EXPECT_EQ(r.action, Reconverter::Action::kSelected);
  EXPECT_EQ(r.selected.value, "鐸");
  EXPECT_EQ(r.selected.spanLength, 1);
  EXPECT_FALSE(recon_->active());
}

TEST_F(ReconverterTest, AnyOtherKeyDismisses) {
  ASSERT_TRUE(recon_->start({"行"}));
  auto r = recon_->feedKey('a');
  EXPECT_EQ(r.action, Reconverter::Action::kDismissed);
  EXPECT_FALSE(recon_->active());

  ASSERT_TRUE(recon_->start({"行"}));
  r = recon_->feedKey(' ');
  EXPECT_EQ(r.action, Reconverter::Action::kDismissed);
  EXPECT_FALSE(recon_->active());
}

TEST_F(ReconverterTest, UnknownOrSelfOnlyValuesDoNotStart) {
  EXPECT_FALSE(recon_->start({"貓"}));  // no reverse entry at all
  EXPECT_FALSE(recon_->active());
  // 獨's only homophone is itself: filtered to an empty menu.
  EXPECT_FALSE(recon_->start({"獨"}));
  EXPECT_FALSE(recon_->active());
}

}  // namespace
}  // namespace mspy
