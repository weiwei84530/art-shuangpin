# 阿特雙拼輸入法 — 專案說明

## 專案目標

Windows 11 原生 TSF 輸入法：**注音式輸入節奏 + 微軟雙拼鍵位 + 微軟注音式的模態選字**。
取代先前在 Rime／小狼毫上的 `mspy_zhuyin` 方案（該方案受限於 Rime 架構：數字鍵無法選字、候選窗無法按需顯示）。

完整計劃與決策記錄：`C:\Users\weiwe\.claude\plans\windows-11-swift-meadow.md`（歷史參考）；本檔為現行狀態的權威來源。

## 架構（全 C++、全 MIT）

```
ime\    SampleIME 衍生 TSF 外殼（微軟官方範例 → 自有碼；候選窗、COM、註冊）
core\   輸入核心（自寫）：雙拼解析、聲調規則、Composing/Selecting 模態狀態機、上下文選字記憶
engine\ 小麥注音引擎子集（gramambular2 詞格轉換 + McBopomofoLM；UserOverrideModel 已不再使用）
data\   小麥詞庫來源（Python 建置 → out\data.txt）
cli\    REPL 測試臺（日常開發主力，不碰 TSF）＋ drill_gen（產生教學網站的看打練習資料）
drills\ 看打練習教材（手寫課程＋自動補完的全鍵位掃描）
web\    互動教學網站＋看打練習（純 CSS/JS，GitHub Pages）
```

- 引擎藏在 `core\` 的介面後（`relaxed_tone_lm` 配接器），日後可抽換。
- **授權紅線：GPL-3 一律不用**。現行全棧 MIT/BSD，來源與 sha 見 `docs/PROVENANCE.md`。

## 輸入方案規格（摘要，完整版見 docs/spec.md）

- 每音節＝微軟雙拼 2 鍵（zh→v、ch→i、sh→u、ing→`;`）＋選擇性聲調數字。
- **單鍵音節（2026-08-08，2026-08-09 擴及全部 26 個字母）**：省略韻母鍵，聲調鍵／空白直接成字。兩類：注音本身就是音節（`z c s r v i u`＝ㄗㄘㄙㄖㄓㄔㄕ、`y w`＝ㄧㄨ、`a e o`＝ㄚㄜㄛ），與**注音的呼名**（`b p m f`＝ㄅㄛㄆㄛㄇㄛㄈㄛ、`d t n l g k h`＝ㄉㄜ…ㄏㄜ、`j q x`＝ㄐㄧㄑㄧㄒㄧ）。字＝`z4`、是＝`u4`、知＝`v`＋空白、**的＝`d`＋空白、了＝`l`＋空白**；仍可接韻母鍵。實作是「省略＝幫你按預設韻母鍵」（`DecodeSingleKey` 轉呼 `DecodeKeyPair`），故 `d` 與 `de` 同讀音、不可能有歧義。`y` 維持 ㄧ，ㄩ 仍是 `yu`／`yy`＋調（**2026-08-10 重新量並拍板不改**：以單字質量計，單音節 ㄧ 1.95% vs ㄩ 1.19%＝1.6:1、整個系列 2.3:1，比舊紀錄寫的 4.1 倍近得多，但 ㄧ 仍居多。ㄩ 是唯一排進前五卻沒有單鍵的音、佔「沒有單鍵」缺口的一半；唯一還空著的第一鍵是 `;`，代價是全形「；」。使用者決定**先不動**。共用候選（打 ㄧ 也給 ㄩ、打 ㄛ 也給 ㄡ）已評估**不可行**：整句轉換照分數挑，常用的那邊一定贏，要拿另一邊還得開選單，比多打一鍵更貴，而且會破壞「單鍵＝預設韻母鍵」這個讓 `d`≡`de` 的保證）。**沒有 ㄅㄜ 這個音節**（詞庫 0 筆），ㄅㄆㄇㄈ 呼名帶 ㄛ。
- **單鍵音節一律用空白／聲調收尾**，不做自動分割（2026-08-08 實作後同日撤銷，理由見狀態記錄，勿再實作）。
- **候選注音由詞庫決定（2026-08-08）**：`DecodeKeyPair` 只做結構過濾，`SyllableInput` 的 validator 查詞庫決定顯示哪個候選、以及該鍵組是否成立（修 `hy` 顯示 ㄏㄩ 等 11 組錯誤＋102 組假注音）。
- **聲調語意（嚴格，已拍板勿反覆）**：不打數字＝只出一聲+輕聲；明打 `1`＝只出一聲；`5`＝只輕聲；2/3/4 精確。
- **聲調鍵左右手鏡像（2026-08-04）**：以 5／6 之間為軸，右手 `0`=一聲、`9`=二聲、`8`=三聲、`7`=四聲、`6`=輕聲，與左手 `1`-`5` 完全等價。
- **未定案窗口（2026-08-08 收回）**：音節第 2 鍵落下不顯示轉換字（`hk`→ㄏㄠ）；**聲調鍵／空白／標點／下一音節首鍵／`` ` ``** 定案成字。**聲調鍵一按即成字**（`hk3`→好），打錯調＝Backspace 刪整個音節重打（無退調）。
- **數字鍵兩種身分（2026-08-08 簡化）**：畫面上還是注音（未定案）＝全部是聲調鍵、`-`/`=` 吃掉；已定案＝`8` 開選單、`9`/`0` 移游標、`-`/`=` 跳頭尾。
- **Tab ＝ Backspace（2026-08-08）**：組字中刪一鍵，閒置時代送真的 Backspace；Shift+Tab 放行（保留反向切換欄位）。
- 模態選字（2026-07-27 大改版）：組字中行內整句轉換；選單內 `1`-`6` 選字（每頁 6）、`7`/`8` 翻頁不環繞、其他鍵關窗並執行原功能；方向鍵在組字中吃掉無作用。**選定後游標跳過該詞段**（可連按 `8` 一路往右改）。
- **選字不動到其他字（2026-08-09）**：套用候選後，所選跨距以外被重走改掉的位置一律釘回原字。詞格的 `overrideCandidate` 會 reset 所有與所選跨距重疊的節點，不修就會發生「不鏽鋼[悲] 選『鋼杯』→ 鏽 變成 秀」。
- **選字記憶＝上下文硬性覆寫、改一次就生效（2026-08-09）**：記「在〈前一／前兩個字〉後面，讀音〈X〉打成〈值〉」，套用時對詞格下高分覆寫（不是加分數）。**無時間衰減**；新選的值一律排到第一名、對手各 −1。檔案 `%APPDATA%\MspyIME\user-choices.txt`。舊的 `UserPreferenceLM` 加分層與 session `UserOverrideModel` 整組移除。
- **Shift 單獨輕按＝中英切換（v5，2026-08-08：不 commit）**：有組字串時整段留著不上屏，切英文後打的字**直接長在同一個未 commit 組字串裡**（literal 讀音），Shift 切回中文可續打中文；只在 Enter／空白（無可定案時）／失焦上屏。英文模式的空白＝字面空白。**沒有組字串時純切模式、按鍵全放行、不補空白**。半形分隔空白只在組字串內部、依游標左邊的字自動補（中→英看中文字、英→中看英數）；v4 的被動記憶（`_lastCharClass`、游標快照、WH_MOUSE hook）已整組刪除。實作走 preserved key＋「鍵盤關閉時仍問 `IsVirtualKeyNeedMspyEnglish`，只有 composer 非空才吃鍵」。**中英模式每個應用程式各自記憶，新 app 一律英文**。
- **空白鍵＝定案不上屏（2026-08-02）**：有待定音節＝以一聲/輕聲預設轉成字、純注音則定案成注音符號（皆留在組字串）；沒東西可定案時才整段 commit（同 Enter）。
- **標點＝定案並融入組字串，不 commit（2026-08-04）**：`,` 打完是「最，」仍在組字串裡、仍可選字；閒置打標點＝開新組字串。**無任何自動上屏界線**，只有 Enter／空白（無可定案時）／Shift 切英文／失焦會上屏。
- 顯示名稱「阿特雙拼輸入法」，zh-TW（LANGID 0x0404），輸出繁體。

## Git 約定（覆寫全域規則）

- **有 remote**：`origin` = https://github.com/weiwei84530/art-shuangpin （公開）。
  **commit 自動；push 與發佈 Release 一律等使用者明確指示。**
- **自動 commit**：使用者授權 agent 全權保管版本，做完一組有意義的修改就 commit，不需徵求同意。
- 直接在當前分支操作，不開分支/worktree。
- Commit message 英文、一行摘要＋必要時補充；**訊息要具體**（使用者靠它回溯，例如 "Port MemoryMappedFile to Win32 file mapping" 而非 "update engine"）。
- **語言分工（覆寫全域「Git 相關描述一律英文」）**：
  - **英文**：commit message、tag 註解、分支名、程式碼與註解。
  - **繁體中文**：`README.md`、**GitHub Release 的標題與內文**、`docs/` 底下所有文件、本檔。
    理由：這些是寫給使用者看的說明，性質與 docs 相同；且 v0.2 起每一版 Release 內文都是中文，
    版本頁的語言必須前後一致。
  - Release 內文沿用前一版的結構（安裝步驟 → 執行階段需求 → 本次改變），可用 `gh release view v0.4` 對照。

## 建置與開發迴圈（詳見 docs/dev-loop.md）

- 工具鏈：VS2022 Build Tools（v143 + Win11 SDK）、CMake、Python 3。
- 詞庫：`scripts\build-data.ps1` → `out\data.txt`。
- 註冊：`scripts\register-dev.ps1`（管理員）；之後重建只需 `scripts\deploy-dev.ps1` 換 DLL + 重開測試 app，**免重註冊**。
- **日常使用走正式安裝** `scripts\install.ps1`（裝到 `C:\Program Files\ArtShuangpin`）；dev 註冊把註冊表綁死在專案資料夾上，搬動專案即失效。兩者共用同一 CLSID、**互斥**，詳見 docs/dev-loop.md。
- 日常開發：`ctest` + `cli\repl` 為主，TSF 實測為輔。

## 工具使用注意

- **讀取含中文的檔案一律用 Read 工具，不要用 `Get-Content`**（PowerShell 主控台編碼會把 UTF-8 中文變亂碼）。必要時先 `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`。
- 詞庫資料鍵的輕聲 `˙` 是**尾綴**（`ㄉㄜ˙`），顯示慣例是前綴（˙ㄉㄜ）——查詢層與顯示層嚴格分離，勿混用。
- 程式註解一律英文；docs 與本檔繁體中文。

## 狀態記錄

- 2026-08-10：**看打練習鍵位稽核（`scripts\check-drill-coverage.ps1`）**。使用者要求驗證「所有鍵位組合都練得到」。
  作法：`drill_gen --audit` 走遍鍵盤（26 個第一鍵 ×27 個第二鍵，經詞庫過濾）得到 **411 個拼得出來的音節**，
  比對課文實際用到的，列出缺口（鍵序＋使用量＋例字），exit 2。**第一次跑出 44 個缺口**——`make-filler-lessons.py`
  原本停在「99% 單字使用量」，所以尾巴整片沒練到（ㄓㄟ 這、ㄍㄨㄣ 滾、ㄏㄚ 哈、ㄕㄟ 誰…）。修法三步：
  (a) 補完的目標集合改由 `--reachable` 提供（不再用使用量門檻）；(b) 沒有常用詞的音節放寬到
  `FALLBACK_WORD_SCORE = -13`，於是 ㄋㄩㄝ 用「虐待」、ㄌㄧㄚ 用「咱倆」、ㄧㄛ 用「喔唷」都練得到
  （**2026-08-09 以為要略過的 ㄋㄩㄝ 其實練得到**；ㄌㄩㄢ 則根本不是任何鍵位拼得出來的音節）；
  (c) 剩下 9 個裡，ㄓㄟ（這，口語音）與 ㄓㄨㄞ（拽）加一句手寫課文，其餘 7 個寫進 `drills\skip-syllables.txt`
  並逐條說明理由（只有生僻字或注音符號本身，連詞都湊不出來）。結果：**404 練得到＋7 明列略過＝411，稽核通過**。
  **同日再修用詞品質**（使用者點名 耒耨欻裒煢衲 讀不出來）：(a) 新增 `MIN_CHAR_SCORE = -6.3`，
  詞裡任一字太生僻就整個詞不用（那六個字都在 −6.4 以下，可接受的 虐咱倆唷嗲剖僧窮 都在 −6.2 以上），
  **兩個 pass 都套用**——沒有可讀的詞就寧可不練；(b) 修好貪婪法的 bug：gain 要算**相異**音節，
  否則「煢煢」算成 2 分贏過「貧窮」；(c) **不再跳過多讀音的詞**，改成把讀音寫死在課文裡（`剖析/ㄆㄡ-ㄒㄧ`），
  原本的跳過規則害 ㄆㄡ 只剩「裒輯」可選；(d) 新增 `drillsvoid-words.txt` 手動排除清單
  （目前只有「什麼」：詞庫四種鍵同分會挑到 ㄕㄜˊ-ㄇㄛ˙，與課文釘的 ㄕㄣˊ-ㄇㄜ˙ 相矛盾）；
  (e) 補完固定 **4 課**、字數平均（原本第 5 課只有 10 個詞）。
  最終：13 課 2134 步、補完用詞 137 個、402＋9＝411 稽核通過，Chrome 實測全跑完無誤。**PowerShell 5.1 的坑**：含中文的 .ps1 必須存成 **UTF-8 with BOM**，
  否則會用 ANSI（本機 Big5）解碼、字串脫序，直接變成語法錯誤。

- 2026-08-10：**鍵盤標註重畫＋看打練習一句一行**。四件事，全在 `web\` 與 `cli\drill_gen.cpp`。
  (1) **O 鍵的 `零` 拿掉**：`INITIALS` 原本給 `o` 標「零」（零聲母），但 a／e 同樣能開零聲母音節卻沒標，
  單標 o 反而讓人以為那是它的聲母。改成 a/e/o 一律只有青色韻母，與使用者「比照 ㄜ 辦理」一致。
  (2) **韻母表補完並改成一行一個讀音**：對照 `core/double_pinyin.cpp` 的 `FinalKeyMap` ×
  `ConsonantFinalZhuyin` 全掃，發現舊表**漏了 ㄐㄑㄒ 專屬的 ㄩ 系讀音**——`u`=ㄩ（居）、`r`=ㄩㄢ（捐）、
  `p`=ㄩㄣ（軍）、`s`=ㄩㄥ（兄）、`v`=ㄩㄝ（絕）全都沒印。補齊後固定排序 plain→ㄧ→ㄨ→ㄩ，
  每個讀音各佔一行右對齊（舊的 "ㄧㄤ ㄨㄤ" 一行 5 字寬過鍵帽會換行，這是「看起來很亂」的主因）。
  (3) **單鍵音節的韻母印在聲母下方**（使用者要求）：`SINGLE_VOWELS` 對 14 個聲母鍵印小字 ㄛ／ㄜ／ㄧ，
  ㄗㄘㄙㄖㄓㄔㄕ 本身就是音節故不印。鍵帽單位 `--u` 50→58px 才塞得下最多四行（`d` 鍵：ㄉ/ㄜ + ㄧㄤ/ㄨㄤ）。
  **可讀性微調（使用者回報小字 ㄧ 讀不出來、ㄛ／ㄜ 分不清）**：注音一律 10px 粗體（呼名韻母與聲母同大小，只差字重——ㄧ 是單一橫劃，一縮小就變成破折號），且**預設傾角 26°→18°**——傾斜是**垂直方向**壓縮，壓的正好是 ㄧ 的那一劃和 ㄛ／ㄜ 的下半部；想看 3D 仍可拖曳轉回去。小視窗改用 `zoom` 整塊縮放（不能只縮 `--u`，注音是固定 px，會讓 `d`／`r` 的四行疊在一起）。
  (4) **句號換行**：句尾按兩次 Enter（上屏＋換行），課文一句一行、記事本跟著捲動、文章區畫淡色 ↵ 並限高捲動
  （否則七句的補完課會把鍵盤擠出畫面）。13 課重新產生（2027 步、58 個換行），覆蓋率不變 366/430。
  以 Chrome 實測 13 課全跑完、輸出與課文完全相同、無 console 錯誤。
  (5) **版面與錯字標記（同日回饋）**：鍵盤放大後會蓋住練習列，改由 `fitKeyboard()` 量剩餘空間即時縮放
  （media query 看不到「上面那一列有多高」，且不能只縮 `--u`）；文章區固定**四行整**；
  打錯的字**打完後顯示橘色**（`drill.slips`，錯鍵記到當下那個字），整課結束報「N 個字打錯過（共 M 字）」。
  (6) **兩處讀音勘誤**：〈聲調〉課的「日子過**得**特別快」與〈合口呼〉課的「傳**得**特別快」都是結構助詞，
  教育部標準是**輕聲 ˙ㄉㄜ**（使用者的習慣是對的），但詞庫 得(ㄉㄜ˙)=−6.8、的(ㄉㄜ˙)=−1.62，
  整句轉換必走「的」，而練習不做選字——標成 ㄉㄜˊ 等於教錯讀音，故**改寫句子避開**。
  「什麼」則相反：教育部三本辭典只收 **ㄕㄣˊ˙ㄇㄜ**，課文原本就是對的（詞庫四種鍵同分，已明寫釘住）。
  (7) **練習模式自己的版面**（同日回饋「垂直方向有點擠」）：`body.drilling` 讓記事本 260→152px、字 24→20px、
  上下留白收緊——那塊只是照出輸入法在做什麼，讓出來的空間剛好夠 `fitKeyboard()` 把鍵盤畫回 zoom 1.0。
  **每課最後一句只按一次 Enter**（`TypeSentence(breakLine)`）：後面沒東西了，上屏就是結束，不再多一個 ↵。
  Enter 總數 116→103、換行 58→45。

- 2026-08-10：**看打練習：按錯出聲**。`web\app.js` 新增 `sound` 模組，Web Audio 現場合成一聲柔和鐘聲，
  **不放音檔**以維持純靜態站台。音色不是憑印象調的：直接量 Win 11 的 Default Beep
  （`C:\Windows\Media\Windows Background.wav`，微軟注音打錯時放的就是它）——FFT 出來是 F3 174.6 Hz 基音
  ＋ C4 261.6 Hz 五度＋ F4／C5 泛音（**不是低頻悶響**），衰減 1.3 秒。`CHIME` 表照這四個分音疊四個 sine，
  各自的延音砍到 0.18–0.45 秒（打字間隔放不下 1.3 秒尾巴），起音 20 ms（原音 60 ms 太慢，回應要落在按鍵上）；
  gain 一律走 exponential ramp，直接設值會有 click。第一版是 196→128 Hz 的下滑三角波，使用者反映太低沉。
  AudioContext 等到第一次要響才建立，那時必定已有使用者手勢。判定放在 `drill.isMistake()`：課程進行中、
  非自動重複（按住不放只算一次錯）、且按鍵是「可能是輸入」的那些（單一字元、空白、Enter、Backspace）才響——
  Shift／Tab／方向鍵／功能鍵一律安靜。**畫面依舊完全不標記錯誤**（2026-08-09 的決定不變），只是多了聲音。
  右上角 🔊／🔇 可關，記在 `localStorage['drillSound']`；按鈕點完 blur，否則空白鍵會再按它一次
  （與 ↻ 同一個坑）。Backspace 在練習中一併 `preventDefault`，避免舊瀏覽器上一頁。以 Chrome 實測：
  錯鍵響、對鍵不響、靜音後不響、關掉再開會回一聲確認、無 console 錯誤。

- 2026-08-09：**M5 回饋第十四輪（單鍵擴及全聲母、選字不亂動、永久上下文記憶、看打練習）**。四件事。
  (1) **單鍵音節擴及全部 26 個字母**：`DecodeSingleKey` 補上 14 個聲母鍵，取「注音呼名的韻母」——`b p m f`→ㄛ、`d t n l g k h`→ㄜ、`j q x`→ㄧ。使用者原話是「ㄅㄜ ㄆㄜ ㄇㄜ ㄈㄜ」，但**ㄅㄜ 在詞庫 0 筆、國語根本沒這個音節**，呼名是 ㄅㄛ ㄆㄛ ㄇㄛ ㄈㄛ，已改正並在 spec 記錄。`m`→ㄇㄛ 不損失「麼」（詞庫裡 麼 同時掛 ㄇㄛ˙ 與 ㄇㄜ˙）。實作直接轉呼 `DecodeKeyPair(first, 預設韻母鍵)`，並有測試綁住「單鍵＝兩鍵」等價，所以不可能產生兩鍵形式表達不了的東西。行為變動：`n`＋空白 從「定案成 ㄋ 符號」變成「呢」（純注音改用 `` n` ``）；`f`＋空白仍定案成 ㄈ（ㄈㄛ 只有二聲）。
  (2) **選字不動到其他字**：使用者回報「改『悲』為『鋼杯』，前面的『鏽』會變回『秀』」。REPL 完整重現（`bu2xq4gh1bz1` → 不鏽鋼**悲**，`8` 後選第 1 個候選「鋼杯」→ **不秀鋼杯**）。根因＝gramambular 的 `overrideCandidate` 會 reset **所有與所選跨距重疊的節點**，三字詞 `不鏽鋼` 涵蓋位置 2 被拆掉，剩下 `不鏽` 重新斷詞成 不＋秀。修法：選字前後比對每個位置的字，**所選跨距以外被改掉的一律以單字覆寫釘回**、重走到穩定（單字覆寫只 reset 該位置，故必然收斂）。自動套用學習覆寫時走同一條保護。
  (3) **學習整組重做成「上下文硬性覆寫」**。舊機制兩個獨立失效都已實測確認：選兩字候選「鋼杯」有學到但**沒有用**（偏好只值 +1e-6，而 `不鏽鋼(-5)+悲(-3)` 本來就贏過 `不(-2)+鏽(-3)+鋼杯(-6.8)`；塞 count=8 的新鮮偏好檔再打一次仍是「不鏽鋼悲」）；選單字「杯」**什麼都沒學到**（舊規則要求左鄰是單字節點，而左鄰是三字詞）。新機制記 `(上下文, 讀音) → 值`，上下文取前一／前兩個**字**（不管它屬於多長的詞），兩種長度都寫、查詢時長的優先；套用是 `kOverrideValueWithHighScore` 節點覆寫，**改一次就一定生效**。無時間衰減：新選的值直接排第一、對手各 −1（上限 8），所以「最近幾次的習慣」贏，且改回去一樣便宜。已覆寫的節點一律跳過，自動修正不會跟使用者當下的選擇吵架。`UserPreferenceLM` 與 session `UserOverrideModel` 刪除；檔案改成 `user-choices.txt`，舊的 `user-phrases.txt` 沒有上下文欄位無法遷移，載入時改名 `.bak` 留著。75 core tests 綠、x64/x86 DLL 已建置。
  (4) **看打練習（`drills\` + `cli\drill_gen.cpp` + `web\`）**。最佳解不能用 JS 猜：產生器自己推導鍵序（單鍵優先、一聲不打數字、**聲調鍵挑跟音節最後一鍵不同手**的鏡像數字），再用**真的 `mspy::Composer` 重跑一遍**，走出來的字跟課文不符就用候選單修（9/0 移游標、8 開窗、8 翻頁、數字選），每一步都存下當下的畫面（含候選窗），網頁端只是播放器。九課手寫課程只到 59% 使用率，`scripts\make-filler-lessons.py` 以貪婪集合覆蓋從詞庫挑 127 個雙字詞補到 **366/433 音節（98.9% 單字使用率）**——尾巴的 ㄋㄩㄝ／ㄌㄩㄢ 依使用者指示略過。網頁：文章區標出當前字（進度用「正確前綴長度」，修字時會指回被修的字）、記事本模擬畫面、3D 鍵盤高亮下一鍵；讀真實鍵盤，**按錯完全沒反應**。以 Chrome 實測 13 課全跑完、輸出與課文完全相同、無 console 錯誤。教學課程也同步更新（單鍵課擴到 26 鍵、空白課改用 ㄋㄧㄠ 舉例、選字課補上兩條新行為）。
  **尚未做**：`scripts\make-package.ps1` 打包與 `install.ps1` 重裝（等使用者指示）；已安裝的輸入法要重裝＋重開應用程式才會吃到這些改動。

- 2026-08-08：**M5 回饋第十三輪（單鍵分割＋注音顯示修正）**。(1) **候選注音改由詞庫決定**（使用者回報「懷」顯示成不存在的 ㄏㄩ）。根因：`DecodeKeyPair` 是**結構超集**（`y` 鍵＝ü／uai、`w` 鍵＝ia／ua…），轉換時 `insertReading` 會依序試到有效的那個，**但顯示層直接取 `candidates_.front()`**，所以畫面顯示 ㄏㄩ、上屏卻是「懷」。改為 `SyllableInput::setValidator()`（`Composer` 建構時注入 `SyllableExists`：查裸讀音＋ˊˇˋ 四次 `hasUnigrams`；裸讀音經 RelaxedToneLM 已涵蓋一聲＋輕聲），`feed()` 用它過濾候選、**全被濾掉就回傳 false**（該鍵組不成立）。以 `decode_dump` 驅動程式對 26×27 鍵組全掃比對詞庫：11 組首候選錯誤（`gd gw gy hd hw hy kd kw ky` ㄍㄎㄏ 不接 ㄧ 介音／ㄩ、`lv nv` 應為 ㄌㄩㄝ/ㄋㄩㄝ）＋102 組兩候選皆不存在（`bb`=ㄅㄡ、`df`=ㄉㄣ、`kz`=ㄎㄟ、`ra`=ㄖㄚ…）全部修掉。**未改 `double_pinyin.cpp` 的結構規則**——原設計就是「結構超集＋詞庫裁決」（見 double_pinyin_test 的 `hw` 註解），手寫音韻表反而會漏掉 ㄉㄣ/ㄎㄟ 這類純詞彙缺口。(2) **單鍵音節自動分割：實作後同日撤銷（勿再實作）**。做法是 `finalizePendingBare()` 把單鍵送進詞格後**不清 `unsettled_`**、接著 `pending_.feed(c)`，`displaySegments()` 的 unconfirmed 天然變成「未定案注音＋pending 注音」（ㄓㄑ → 知ㄑㄧㄥ），零額外狀態、測試全綠——知情＝`vq;`、資訊＝`zxp`。**撤銷理由（使用者實測）**：分割只在「兩鍵拼不出任何存在音節」時成立，而 `v`+`u`＝ㄓㄨ 是有效音節，所以知識永遠得隔開；同一組肌肉記憶在知情能用、在知識會失控。改回「單鍵音節一律接空白或聲調」，一致性優先。**詞庫全掃結果（保留供參考）**：兩字都是單鍵音節且第一字無聲調的詞，144 種單鍵配對有 86 種撞到有效音節，對應 62 個詞需要隔開（一直 `y`＋空白＋`v2`、支持 `v i2`、知識 `v u4`、之一、醫師、師資、獅子、屋子、姿勢、失誤…；「一」開頭佔近四分之一），另 21 個詞的兩鍵拼不出音節（一次 `yc4`、阿姨 `ay2`、詩詞 `uc2`…）。明打一聲 `1` 與空白等效（`v1u4`＝知識）。82 core／180 全部測試綠。規格見 docs/spec.md §1、§3.0。

- 2026-08-08：**M5 回饋第十二輪（四項互動調整）**。(1) **聲調鍵按下即成字**（撤銷 2026-08-04 的「打了調仍維持注音」）：`Unsettled` 只剩「還沒給聲調」一種，`toneGiven`／`undoUnsettledTone`／`keys` 移除，打錯調改為 Backspace 刪整個音節；數字鍵規則因此簡化為兩態。(2) **單鍵音節**：`DecodeSingleKey()` 給 12 個「注音本身即音節」的首鍵，`SyllableInput::convertible()`（＝候選非空）取代 `complete()` 成為「可轉換」判準——`complete()` 仍決定能不能再吃一個字母，故 `u`＋`l`＝ㄕㄞ 不受影響；Enter 也一併轉換單鍵。`y` 維持 ㄧ（以 out\data.txt 統計：ㄧ 系音節總質量 0.0411 vs ㄩ 系 0.0100，單音節 ㄧ 0.0159 vs ㄩ 0.0039；且詞格節點只能有一個讀音，無法讓單鍵同時是 ㄧ 又是 ㄩ）。(3) **Tab ＝ Backspace**（閒置時走 `FUNCTION_NAV_INJECT` 注入 VK_BACK，注意 Backspace 不加 `KEYEVENTF_EXTENDEDKEY`；Shift+Tab 放行）。(4) **中英切換不再 commit**：關鍵發現＝`_IsKeyEaten` 在鍵盤關閉時是**我們自己**選擇不吃鍵，TSF 照樣送 key event，所以可以「工具列顯示英、同時繼續吃鍵維持組字」。新增 `IsVirtualKeyNeedMspyEnglish`（只在 composer 非 kEmpty 時吃可見 ASCII，保留大小寫）＋`FUNCTION_ENGLISH_INPUT` → `Composer::feedEnglishChar`（literal 讀音，與標點同機制）＋`Composer::switchLanguage(toEnglish)`（settle＋依游標左邊字元補半形空白）。v4 被動記憶整組刪除（`_lastCharClass`、`_ObserveBypassedKey`、commit 游標快照、執行緒 WH_MOUSE hook、TextEditSink 的純游標移動偵測）。順修 `RelaxedToneLM` 的 literal 判定：原本用「不含 `-`」排除跨節點鍵，導致 literal `-` 本身無法輸入，改為「前綴後恰好一個碼位」。80 core tests 全綠、x64/x86 已建置。規格見 docs/spec.md §1、§5、§6。

- 2026-08-04：**tag v0.4 並發佈 GitHub Release**（附 art-shuangpin-v0.4.zip）。內容＝第十一輪互動調整＋智慧選字重做＋安裝程式三項改進。安裝程式：(1) 開頭清掉先前停放的 `.old.<8碼hex>`（每次升級約 8 MB，本機曾累積 5 個／24 MB）；(2) `Test-SameFile`（長度＋SHA256）相同則跳過複製，升級不再重複停放 7.5 MB 詞庫；(3) 安裝前檢查 `VCRUNTIME140/VCRUNTIME140_1/MSVCP140`（**x86 側不需要 `VCRUNTIME140_1`**，x64 專用）並印出下載連結——**缺少 VC++ 2015-2022 可轉散發套件時輸入法會出現在清單裡但完全無反應**（靜默失敗，與註冊問題難以區分）。DLL 走 /MD 共用 CRT（vcxproj 未指定 `RuntimeLibrary`＝MSVC 預設）。
- 2026-08-04：**智慧選字重做（使用者回報「選字記不起來」）**。根因：舊機制把 `值 讀音鍵` 餵給 McBopomofo `UserPhrasesLM`，**每筆分數都是 0**（字典是負的對數機率），walk 用嚴格 `>`＋`stable_sort`＋按行序載入 → **同一讀音鍵下最早寫入的永遠贏**；後選的修正打不過、一次性情境選擇永久綁架該讀音，學習檔甚至讓輸出比不學習還差（實測 `wo3vidk4` 純字典「我知道」、載入學習檔變「我之道」；使用者檔案 28 筆裡 5 組卡住）。改為自寫 `core\user_preferences`（`值 讀音鍵 次數 最後使用秒數`，權重＝次數每 14 天減半、低於 0.5 即失效、上限 32；`record` 從**衰減後**權重加、`touch` 於上屏時只更新時間戳讓常用詞不衰減；存檔前 `mergeFrom` 磁碟現況再 `MoveFileEx` 原子換檔，因每個 app 各有一份 TIP 實例）＋ `core\user_preference_lm`（夾在 Composer 與 RelaxedToneLM 之間，把有效偏好排到該讀音字典最佳分數 +1e-6，margin 極小故不干擾跨跨距競爭；字典沒有的詞以「逐字拼出成本」為基準）。**單字選擇改學上下文兩音節詞**（我在家選「在」→ 記「我在」），左鄰優先、只配單字節點、不跨標點。舊檔遷移：兩欄位行給遷移當下時間戳保住既有詞彙，**同鍵多筆且全為舊格式者整組刪除**。`repl --user-phrases` 新增供離線重現排序。75 core tests 全綠。規格見 docs/spec.md §7。
- 2026-08-04：**M5 回饋第十一輪（三項互動調整）**。(1) **未定案窗口擴大到含聲調**：`lastWasBare_`／`lastBareSyllables_`／`lastBareDisplay_` 三個旗標整併為 `Composer::Unsettled`（`active`／`toneGiven`／`syllables`／`keys`／`display`），聲調鍵套用後保持未定案、顯示注音＋調號（明打一聲用 `ˉ` 哨兵、輕聲尾綴 `˙`，display 恰等於詞格裡的讀音）；定案觸發改為空白／標點／下一音節首鍵／`` ` ``。(2) **標點融入組字串**：`DirectPunctuation`／引號從「`takeCommitText` + 符號」改為 `settlePending()` + `insertLiteralText()`（與 `` ` `` 定案注音同一條 literal 路徑），閒置打標點開新組字串；無自動上屏界線。(3) **聲調鍵鏡像**（`ToneDigit()`：0=1、9=2、8=3、7=4、6=5），與 `8`/`9`/`0` 的衝突以未定案窗口切開——未定案時數字全是聲調鍵、給調後全部吃掉（含 `-`/`=`），定案後才是控制鍵；改調走 Backspace 退調（`undoUnsettledTone()`，無無聲調讀音者退回 pending）。148 tests 全綠、x64/x86 已建置（`ime\{x64\,}Release\SampleIME.dll`），教學網站 `web\` 已同步改寫（標點課重建、聲調課補鏡像鍵與 Backspace 退調、鍵帽 6-0 補調號）並以 Chrome 實測。**注意：build/ 與 build32/ 的 CMake 快取仍綁在舊路徑 `D:\Claude\Input`，已刪除重新 configure。**規格見 docs/spec.md §5、§6。已 `make-package.ps1` 打包並以管理員跑 `install.ps1` **安裝到 `C:\Program Files\ArtShuangpin`**（x64/x86 皆更新、CLSID 註冊路徑不變）。安裝時舊 DLL 被執行中的 TSF 宿主鎖住，依 `Copy-Payload` 的既定升級路徑改名為 `*.old.<8碼>` 留在原地——重開機後可安全刪除；**已載入輸入法的應用程式要重開才會吃到新 DLL**。
- 2026-08-04：**開發機改為正式安裝**。專案資料夾從 `D:\Claude\Input` 搬到 `D:\Projects\art-shuangpin` 後輸入法失效＝這台機器一直只跑 dev 註冊（`InprocServer32` 指向 `out\deploy\` 絕對路徑），舊路徑消失 → COM 載不到 DLL。已解壓 `out\art-shuangpin-v0.3.zip` 以管理員跑 `install.ps1`，安裝到 `C:\Program Files\ArtShuangpin`（CLSID x64/x86 與 IconFile 皆已改指系統路徑）。互斥關係與診斷指令補進 docs/dev-loop.md。
- 2026-08-02：**tag v0.3 並發佈 GitHub Release**（附 art-shuangpin-v0.3.zip）。內容＝第十輪四項互動調整（注音維持顯示、空白鍵定案、選字後游標右移、per-app 中英記憶）＋候選窗 DPI 縮放修正；教學網站已隨 push 自動重佈。
- 2026-08-02：**候選窗 DPI 縮放修正＋教學網站同步更新**。(1) 候選窗在 150%／175% 縮放螢幕上特別小＝所有尺寸寫死 96 dpi、字型又吃行程層級的 `Global::defaultlFontHandle`（per-monitor aware 宿主如 LINE/Chrome 回報 96）。改為 `CCandidateWindow` 自己持有 per-DPI 字型與度量（`_UpdateMetricsForDpi`：`GetDpiForWindow` → 重建 10pt 字型、列高/頁碼列/邊框 `MulDiv(…, dpi, 96)`），在 WM_CREATE、每次 `_ResizeWindow`（＝presenter 定位後）與 `WM_DPICHANGED` 重算。(2) `web\` 教學腳本補齊到現行規格：組字改單色（移除藍色 `--pending`）、待定音節顯示注音、新增〈空白鍵：定案與上屏〉一課（共 8 課）、選字後游標右移並可連按 `8`、Shift 課補 per-app 模式記憶、Space 鍵帽標註「定案」；候選清單改用 REPL 實跑的真實候選。已用 Chrome 實測。
- 2026-08-02：**M5 回饋第十輪（四項互動調整）**。(1) **中英模式改 per-app 記憶**：TIP 本來就跑在各應用程式行程內，改為新 app 一律英文（`InitializeSampleIMECompartment` 設 FALSE，原本設 TRUE）＋焦點回來時把記憶值寫回 OPENCLOSE compartment（ThreadMgr/KeyEvent 兩個 OnSetFocus），蓋掉系統的跨 app 同步；記憶只由 Shift 輕按更新（工具列點擊無法與系統寫入區分，故不納入）。(2) **選字後游標跳到該詞段之後**（`chosen.location + spanningLength`）——可連按 `8` 一路往右改完整句。(3) **最新音節維持注音顯示**：eager 進詞格照舊（整句轉換不變），但顯示層把該字蓋成注音（`lastBareDisplay_`），聲調／空白／下一音節首鍵／移游標／開選單才現字。(4) **空白鍵改「定案」不 commit**：完整音節以一聲/輕聲預設轉字、查無無聲調讀音的注音（`n`、ㄋㄧㄠ）定案成注音符號融入組字串（走 `` ` `` 同機制）；**沒東西可定案時才整段 commit**（使用者選擇保留舊習慣）。143 tests 全綠、x64/x86 已建置部署。規格見 docs/spec.md §6。
- 2026-07-29：**tag v0.2.1 並發佈 GitHub Release**（附 art-shuangpin-v0.2.1.zip 安裝包）。內容＝第八、九輪全部改進：閒置導航鍵、NumPad 豁免、LINE 反白修正、單色顯示、Shift 空白被動記憶（含三段演進：OnTestKeyDown 目擊修正、純游標移動判定＋commit 游標快照、**執行緒內 WH_MOUSE hook 滑鼠歸零**——LINE/Sublime 不回報游標事件的最終解）。
- 2026-07-29：**M5 回饋第九輪（Shift 空白改被動記憶＋移除藍色）**。(1) **Shift 分隔空白 v4＝純被動記憶**：確認讀取游標周邊文字只有完整 TSF app（記事本/Word）支援——Chromium 系（LINE/Chrome）text store 只暴露組字段、Sublime 走 IMM32、Terminal 最小化實作，v3「讀文件」在多數 app 失效。改為完全不讀文件：記憶「自己 commit 的尾字＋英文模式目擊放行的按鍵」（`_lastCharClass`），保守歸零（焦點切換/導航鍵/Ctrl-Alt 組合/app 回報 selection change 排除自身編輯 `_ownDocEditPending`/組字被點擊終止），未知不補——寧缺勿濫。(2) **藍色（聲調未定/殘鍵）換色整個移除**：同因只有完整 TSF app 會畫顏色，組字全段改 app 預設色＋虛線底線；黑/藍分段語意留在內部 segments（游標/錨點計算沿用）、選字錨點淡藍反白保留。142 tests 全綠、x64/x86 已建置部署。規格見 docs/spec.md §6〈中英切換〉v4、〈單色顯示〉。
- 2026-07-28：**M5 回饋第八輪（三項功能＋LINE 反白修正）**。(1) **LINE 定調字黑色反白修正**：定調黑段顯示屬性 `TF_ATTR_TARGET_CONVERTED`→`TF_ATTR_INPUT`（Chromium 系把前者畫成選取反白並忽略色彩；錨點淡藍維持 TARGET_CONVERTED 不改）。(2) **閒置導航鍵**：無組字時 `9`/`0`（Shift 未按）代送 ←/→、`-`/`=`（不分 Shift）代送 Home/End（SendInput 注入、實體 Shift 自然成選取）；組字中 `-`/`=` 新增游標跳組字串頭/尾；Shift+9/0 保留（）故無字元級選取（拍板）。閒置 `_`→「——」對映因此消失（組字中不受影響）。(3) **NumPad 豁免**：VK_NUMPAD0-9 與 0x6A-0x6F 閒置放行字面輸入、組字中先整段上屏再輸出（`FUNCTION_NUMPAD_COMMIT`）。順修 ParselessPhraseDB `reverseFindRows` 檔末無換行漏配對的 off-by-one。**決策記錄（同日撤銷，勿再實作）**：曾完整實作「重選字」（閒置按 `8` 對既有已上屏文字反查同音字、在既有文字上開 composition 原地取代，含 core\reconverter 類與引擎 `getReadings` 曝光），實測**大多數應用程式不支援**（只有文字編輯器可用），整組 revert 移除，閒置 `8` 回歸吃掉無作用。142 tests 全綠、x64/x86 已建置部署。規格見 docs/spec.md §6〈閒置導航鍵〉。

- 2026-07-27：**互動教學網站（web\）**。純 CSS 3D transforms＋vanilla JS 零依賴：左側欄選教學主題、上方仿記事本畫面（組字雙色/虛線底線/錨點反白/候選窗含下方放不下自動翻上）、下方 3D 鍵盤（滑鼠拖曳轉動、角度夾住翻不到背面、鍵帽標註聲母橘/韻母青/控制鍵灰、按鍵有按下動畫）。七組腳本化教學（總覽/基礎/聲調/選字/Shift/標點/注音），步驟資料在 tutorials.js（screen 狀態採前態合併）。本機預覽：`python -m http.server -d web`。已以 Chrome 實測驗證。回饋修正：機殼側面 rotateY 方向反轉重建五面體、鍵帽改三層機械式結構、播放器按鈕補接事件、游標隨 9/0 移動（screen 狀態 `cur` 欄位）、按鍵動畫放慢 1.5 倍。**已部署 GitHub Pages**（Actions workflow `.github/workflows/pages.yml`，push main 的 web/ 變更即自動重佈）：https://weiwei84530.github.io/art-shuangpin/

- 2026-07-27：**GitHub 公開準備**。(1) 英文名定案 `art-shuangpin`（阿特＝art）。(2) 全部 commit 歷史作者改寫為 GitHub noreply（`weiwei84530 <150053178+weiwei84530@users.noreply.github.com>`），全域 git config 同步更新。(3) 新增根目錄 LICENSE（MIT, Weiwei）與精簡 README.md。(4) 新增 `scripts\make-package.ps1`（打包 x64/x86 DLL→改名 ArtShuangpin.dll＋mspy-data.txt＋install.ps1 成 zip）與 `scripts\install.ps1`（目標機免建置工具：裝到 Program Files、icacls AppContainer、regsvr32 註冊；`-Uninstall` 反向）——供另一台 Win10 安裝。已公開於 https://github.com/weiwei84530/art-shuangpin（remote：origin），主分支改名 `main`，全域 `init.defaultBranch` 同步設為 main。**本專案自此有 remote：commit 後照舊自動，push 仍等使用者指示。**同日打 `v0.2` 標籤並發佈 GitHub Release（附 `art-shuangpin-v0.2.zip` 安裝包）。
- 2026-07-27：**M5 回饋第七輪**。(1) Shift 分隔空白：數字歸英文類（切中文時左邊是 0-9 也補空白）。(2) **中文模式數字排整個禁用**（無組字時數字吃掉無作用；組字中維持控制鍵）——要打數字＝先 Shift 切英文，空白自動處理，養成習慣。138 tests 全綠。
- 2026-07-27：**M5 回饋第六輪**。Shift 分隔空白改「無狀態」判定：切換當下用 TSF 讀游標左邊字元（組字中則看 commit 尾字）——切英文左邊是中文字才補空白、切中文左邊是英文字母才補，其他（空白/數字/標點/讀不到）不補；typed-since-boundary 追蹤機制整組移除。
- 2026-07-27：**M5 回饋第五輪**。選字窗貼近螢幕底部會超出去→修正：`_MoveWindowToTextExt` 改走樣本既有的 `CalcFitPointAroundTextExtent`（工作區內下方放不下自動翻到錨點字上方），且每次候選列表更新（高度改變）後重新定位。
- 2026-07-27：**M5 回饋第四輪**。(1) 詞放寬確認為使用者誤解，**拍板不採用**、清乾淨（alias 腳本刪除、build-data 還原，spec 留一行決策記錄防反覆）。(2) 注音 v3：`` ` `` 改為「定案」語意——待定注音按 `` ` `` 轉黑融入組字串（literal 讀音 `\x01`+符號，每碼位一節點）、`` ` ``+韻母鍵直接定案（ㄋㄧㄠ＝``n`y``k`` 一次組）；Space 上屏藍殘鍵的 v2 通則保留。(3) 選單過濾無效候選（值＝該跨距現顯示文字者隱藏；Candidate 加 location/spanningLength，見 reading_grid [mspy] 註記）；過濾後空選單不開。138 tests 全綠。
- 2026-07-27：**M5 回饋第三輪**。(1) 含輕聲詞放寬**暫時停用**（使用者改測 er2zi 實調工作流；build-data 第 4 步註解保留，`add_neutral_phrase_aliases.py` 留檔待決）。(2) 注音輸入改版：Space 上屏可見注音殘鍵（`n`+Space→ㄋ、Enter 仍丟棄）；`` ` ``＝挖空聲母、下一鍵讀為韻母（`` \`k ``→ㄠ，`HollowFinalDisplay` 全鍵表）；舊的 ` 前導模式移除。(3) 圖示換紅底白字「特」（`scripts\make_icon.py` 產生 SampleIme.ico 全尺寸；免重註冊，圖示快取可能需重開 explorer/登出才更新）。136 tests 全綠。
- 2026-07-27：**M5 回饋第二輪**。(1) 含輕聲的詞放寬（方案 2）：build-data 第 4 步 `add_neutral_phrase_aliases.py` 為含 `˙` 的詞鍵加「其他音節去調」alias（+3649 列）——`erzi`→兒子、`ufme`→什麼；不含輕聲的詞維持嚴格。(2) 選字窗改開在游標錨點字正下方（layout 追蹤錨點子範圍）。(3) 新增 `` ` `` 前導注音字面模式（`` `b ``→ㄅ、`` `ok ``→ㄠ、`` `ul3 ``→ㄕㄞˇ；Space/Enter 上屏、Esc 取消；組字中按 `` ` `` 先 commit 前段）。136 tests 全綠。
- 2026-07-27：**M5 實測回饋修正（v2）**。(1) Shift 切換失效根因＝樣本把「Shift 單獨放開」註冊為 TSF preserved key（IME-mode），TSF 在 KeyEventSink 前攔截並關掉輸入法；改為在 OnPreservedKey 接管執行新流程，KeyEventSink 偵測法移除。(2) Shift 語意改 commit 式（見規格摘要）；底線內英文段機制（literal 讀音）整個移除，composer 回歸純中文。(3) 選字窗跑到左上角＝_ResizeWindow 重設座標為 (0,0) 的回歸，改為保留現位置。(4) 首鍵顯示對齊：y→ㄧ、w→ㄨ、a→ㄚ、e→ㄜ、o→ㄛ。133 tests 全綠。已知：Sublime Text 不渲染 TSF display attribute（無底線/反白/顏色），屬應用程式限制、無解於輸入法端。
- 2026-07-26：專案啟動。計劃核准（架構歷經三輪收斂：fork 新酷音 → 自建本體+chewing 函式庫 → 因 GPL-3 紅線改為 SampleIME 外殼 + 小麥引擎全 MIT 棧）。
- 2026-07-26：**M1 完成**——引擎+詞庫函式庫化驗證通過（CLI：ㄒㄧㄣㄎㄨˋㄧㄣ…→新酷音輸入法；data.txt 需 LF，build-data.ps1 已處理）。**M2 完成**——core\ 四模組 + 124 tests 全綠；REPL `--keys` 實測：`ni3hk3`→你好（含整句自動修正 你蒿→你好）、`de`→的、`wo`→窩/`wo3`→我、`{`開窗數字選字、`,`→「，」。設計要點：**音節第二鍵落下即以無聲調 eager 進詞格（見字即所得），聲調數字對剛進格音節回填**。
- 2026-07-26：**M0d 完成**——SampleIME retarget v143（WPO off、C4463 bitfield 修正）雙架構建置；LANGID 已提前改 0x0404（掛在中文(台灣)下，免裝簡中）。已部署至 out\deploy 並以管理員註冊（HKCR/CTF TIP 已驗證）。
- 2026-07-26：**M0e 完成（使用者實測通過）**。兩個 Win11 坑已修：(1) `ITfContextView::GetWnd` 回 NULL → 候選窗無主不顯示，`GetFocus()` 後備（CandidateListUIPresenter.cpp）；(2) `TEXTSERVICE_DIC` 是**DLL 同層**的檔名（無子資料夾），部署時勿放 Dictionary\。除錯基礎設施：`MSPY_DEBUG_LOG`（Private.h 開關）寫 `%TEMP%\MspyIME.debug.log`。
- 2026-07-27：**M3 主體接線完成**，設計見 docs/m3-design.md。要點：MspyBridge（ime\）持有 LM+Composer；`IsVirtualKeyNeedMspy` 全面接管按鍵路由（原樣本邏輯 #if 0 保留）；KeyHandler 各 `_Handle*` 改為「餵 composer + `_SyncComposer`」單一模式；Esc/↑ 在選字時走 CANDIDATE/FINALIZE_CANDIDATELIST（只關窗）；數字選字以「頁內編號→字串→比對 composer 候選」解析；詞庫部署為 DLL 同層 `mspy-data.txt`。vcxproj：/utf-8 **/permissive**（樣本舊碼過不了 C++20 嚴格模式）、C++20、連 build\ 與 build32\ 的 mspy_core+mcb_engine。使用者實測迭代修正：候選窗（GetWnd 後備＋詞典路徑）、`_AddComposingAndChar` 以選取位置裁切組字範圍造成游標中移時右側文字重複（改 `_SetCompositionText` 直設整段）、**OnCompositionTerminated 改為保留文字（點他處＝自動上屏）並重置 composer**、標點全表（雾凇式，引號開閉交替）、雙色顯示（黑＝聲調已定、藍＝未定；**全段虛線底線直到 commit**）、←/→ 組字內移動游標、空白＝commit（與 Enter 同）。
- 2026-07-27：**M5 完成（互動大改版＋正名「阿特雙拼輸入法」）**。(1) 選字改全數字操作：`9`/`0` 游標左右（環繞）、`8` 開選單（游標錨點反白＝新 TSF 顯示屬性淡藍底）、每頁 6 候選 `1`-`6` 選、`7`/`8` 翻頁不環繞、任何其他鍵關窗並執行原功能；←/→/↑/↓/Esc 選字功能全移除（組字中方向鍵吃掉、Esc=全取消）。(2) Shift 單獨輕按中英切換（KeyEventSink 按下/放開偵測，Shift+字母不誤觸）；組字中切換自動插空白、英文段以 literal 讀音（`\x01` 前綴哨兵、RelaxedToneLM 解析）進詞格、底線維持。(3) 候選窗美化：白底、DWM 圓角、細邊框、選中列淡藍、灰數字、右下頁碼、高度貼合內容、微軟正黑體、移除捲軸。(4) 正名只改顯示名稱（Register.cpp TEXTSERVICE_DESC），GUID 全部不動；**需以管理員重跑 register-dev.ps1 更新註冊表名稱**。138 tests 全綠。
- 2026-07-27：**M4 完成，tag v0.1**。UOM 學習（容量 500、半衰期 90 分；手動選字 observe、每次插入後 suggest 回套）；多音節手選詞持久化 `%APPDATA%\MspyIME\user-phrases.txt`（附加＋去重＋重載；單字僅 UOM 工作階段學習，避免 score-0 永久壓排序）；**正名「Wei雙音拚輸入法」，13 組 GUID 全部重生成（新 CLSID 22DFB512-5772-4938-9FF1-EE24B3904B74），舊 CLSID 已反註冊**。127 tests 全綠。
