# 阿特雙拼輸入法 — 專案說明

## 專案目標

Windows 11 原生 TSF 輸入法：**注音式輸入節奏 + 微軟雙拼鍵位 + 微軟注音式的模態選字**。
取代先前在 Rime／小狼毫上的 `mspy_zhuyin` 方案（該方案受限於 Rime 架構：數字鍵無法選字、候選窗無法按需顯示）。
**2026-08-13 起同一個 repo 也含 macOS 版**（`mac\`，InputMethodKit 外殼，共用同一套 `core\`）。

完整計劃與決策記錄：`C:\Users\weiwe\.claude\plans\windows-11-swift-meadow.md`（歷史參考）；本檔為現行狀態的權威來源。

## 架構（全 C++、全 MIT）

```
ime\    SampleIME 衍生 TSF 外殼（微軟官方範例 → 自有碼；候選窗、COM、註冊）
mac\    macOS InputMethodKit 外殼（ObjC++）；與 ime\ 平行，共用 core\ 與 engine\
core\   輸入核心（自寫）：雙拼解析、聲調規則、Composing/Selecting 模態狀態機、上下文選字記憶
engine\ 小麥注音引擎子集（gramambular2 詞格轉換 + McBopomofoLM；UserOverrideModel 已不再使用）
data\   小麥詞庫來源（Python 建置 → out\data.txt）
cli\    REPL 測試臺（日常開發主力，不碰 TSF）＋ drill_gen（產生教學網站的看打練習資料）
drills\ 看打練習教材（手寫課程＋自動補完的全鍵位掃描）
web\    互動教學網站＋看打練習（純 CSS/JS，GitHub Pages）
```

- 引擎藏在 `core\` 的介面後（`relaxed_tone_lm` 配接器），日後可抽換。
- **授權紅線：GPL-3 一律不用**。現行全棧 MIT/BSD，來源與 sha 見 `docs/PROVENANCE.md`。

## macOS 外殼（mac\）

`mac\` 之於 macOS，等同 `ime\` 之於 Windows：**只是外殼**。所有輸入行為都在 `mspy::Composer` 裡，
兩邊都只是餵鍵、畫畫面。行為要改就改 `core\`，不要在任何一邊的外殼裡重寫。
詳細規則見 `mac/CLAUDE.md`（英文），工程記錄見 `mac/docs/NOTES.md`。

**四組必須同進退的接縫**（改了左邊就要看右邊）：

| Windows | macOS |
|---|---|
| `CompositionProcessorEngine.cpp` 的 `IsVirtualKeyNeedMspy` | `mac/src/ArtInputController.mm` 的 `-handleKeyDown:client:`　**順序有意義** |
| 同檔的 `IsVirtualKeyNeedMspyEnglish` | `-handleEnglishKeyDown:client:shift:` |
| `MspyBridge.cpp` 的 `Load`／`SavePreferences` | `ArtBridge.mm:249-346`（`MoveFileEx`→`std::rename`） |
| `CMakeLists.txt` 的 `mcb_engine`／`mspy_core` 來源清單 | `mac/Makefile` 的 `ENGINE_SRC`／`CORE_SRC` |

前兩組是**逐條音譯**，工具查不出語意漂移，所以 `mac/upstream-alignment.txt` 記了那兩個函式 body 的 sha256。
改完 `core\` 或 `ime\` 的鍵路由，跑 **`python scripts\check-parity.py`**（`--fix` 只修機械性的部分）。
**發佈前一定要跑**，見下面 Git 約定那一節。

其他要點：

- **不要在這台機器建置 `mac\`**（沒有 darwin 工具鏈）。`core/ engine/ cli/` 的改動在這裡用 `ctest` 驗；
  `mac/src/**` 只有 CI 會編、只有使用者的 Mac 會跑。macOS 上預期 **171** 個測試（比這裡少 2 個：
  `MemoryMappedFile` 的測試依平台二選一）。
- **不要跑 `git add --renormalize`**。`.gitattributes` 只涵蓋 `mac/**`、`*.sh`、`*.command`、workflow YAML；
  Windows 那半刻意不動（它是 `core.autocrlf=true` 的混雜狀態）。renormalize 是唯一能把這件事變成
  全樹 diff 的指令。
- **`VERSION` 是唯一版本來源**。`mac/Makefile` 蓋進 `Info.plist`、`make-package.ps1` 用它命名 zip。
- **打 tag `vX.Y.Z` 會觸發 macOS 的 release workflow ＝ 等同發佈動作**，所以照 Git 規則要等使用者指示。
  Windows 那半仍在本機手動打包後上傳到同一個 Release（兩份資產：`art-shuangpin-vX.Y.Z.zip` 與
  `art-shuangpin-mac-vX.Y.Z.zip`）。
- 詞庫在 macOS 上用 `scripts/build-data.sh` 建（`build-data.ps1` 的對應版本，實測產物位元組相同）。
- 圖示有兩份、畫同一個「特」字：`scripts\make_icon.py`（Windows `.ico`）與 `mac/tools/make_icon.m`
  （macOS TIFF，長寬比 1.375）。改了一邊記得看另一邊——沒有工具會提醒。

**語言依讀者而非目錄。** `mac/CLAUDE.md` 與 `mac/docs/NOTES.md` 維持英文：讀者是要改 `mac/src/` 的人，
整套詞彙（IMKInputController、marked text、TCC、code directory hash）本來就是英文，翻譯只會更難用，
而且會切斷它們與 Windows 端英文註解逐句對照的關係。其餘一律照舊：`README.md`、根 `docs/**`、
`mac/docs/INSTALL.md`、GitHub Release 標題與內文、以及 `*.command`／`*.txt` **印給使用者看的字**，
全部繁體中文。

## 輸入方案規格（摘要，完整版見 docs/spec.md）

- 每音節＝微軟雙拼 2 鍵（zh→v、ch→i、sh→u、ing→`;`）＋選擇性聲調數字。
- **單鍵音節（2026-08-08，2026-08-09 擴及全部 26 個字母）**：省略韻母鍵，聲調鍵／空白直接成字。兩類：注音本身就是音節（`z c s r v i u`＝ㄗㄘㄙㄖㄓㄔㄕ、`y w`＝ㄧㄨ、`a e o`＝ㄚㄜㄛ），與**注音的呼名**（`b p m f`＝ㄅㄛㄆㄛㄇㄛㄈㄛ、`d t n l g k h`＝ㄉㄜ…ㄏㄜ、`j q x`＝ㄐㄧㄑㄧㄒㄧ）。字＝`z4`、是＝`u4`、知＝`v`＋空白、**的＝`d`＋空白、了＝`l`＋空白**；仍可接韻母鍵。實作是「省略＝幫你按預設韻母鍵」（`DecodeSingleKey` 轉呼 `DecodeKeyPair`），故 `d` 與 `de` 同讀音、不可能有歧義。`y` 維持 ㄧ，ㄩ 仍是 `yu`／`yy`＋調（**2026-08-10 重新量並拍板不改**：以單字質量計，單音節 ㄧ 1.95% vs ㄩ 1.19%＝1.6:1、整個系列 2.3:1，比舊紀錄寫的 4.1 倍近得多，但 ㄧ 仍居多。ㄩ 是唯一排進前五卻沒有單鍵的音、佔「沒有單鍵」缺口的一半；唯一還空著的第一鍵是 `;`，代價是全形「；」。使用者決定**先不動**。共用候選（打 ㄧ 也給 ㄩ、打 ㄛ 也給 ㄡ）已評估**不可行**：整句轉換照分數挑，常用的那邊一定贏，要拿另一邊還得開選單，比多打一鍵更貴，而且會破壞「單鍵＝預設韻母鍵」這個讓 `d`≡`de` 的保證）。**沒有 ㄅㄜ 這個音節**（詞庫 0 筆），ㄅㄆㄇㄈ 呼名帶 ㄛ。
- **單鍵音節一律用空白／聲調收尾**，不做自動分割（2026-08-08 實作後同日撤銷，理由見狀態記錄，勿再實作）。
- **候選注音由詞庫決定（2026-08-08）**：`DecodeKeyPair` 只做結構過濾，`SyllableInput` 的 validator 查詞庫決定顯示哪個候選、以及該鍵組是否成立（修 `hy` 顯示 ㄏㄩ 等 11 組錯誤＋102 組假注音）。
- **聲調語意（嚴格，已拍板勿反覆）**：不打數字＝只出一聲+輕聲；明打 `1`＝只出一聲；`5`＝只輕聲；2/3/4 精確。
- **聲調鍵左右手鏡像（2026-08-04，2026-08-17 少一個鍵）**：以 5／6 之間為軸，右手 `0`=一聲、`9`=二聲、`8`=三聲、`7`=四聲，與左手 `1`-`4` 完全等價。**輕聲只剩左手 `5`**——`6` 已整個讓給 Backspace（見下一條）。
- **未定案窗口（2026-08-08 收回）**：音節第 2 鍵落下不顯示轉換字（`hk`→ㄏㄠ）；**聲調鍵／空白／標點／下一音節首鍵**定案成字（`` ` `` 曾經也是，2026-08-17 移除）。**聲調鍵一按即成字**（`hk3`→好），打錯調＝Backspace 刪整個音節重打（無退調）。
- **數字鍵兩種身分（2026-08-08 簡化，2026-08-14 換掉閒置那一半，2026-08-17 把 `6` 拉成通用刪除鍵）**：畫面上還是注音（未定案）＝聲調鍵，**但 `6` 是 Backspace**；已定案＝`5` Delete、`6` Backspace、`8` 開選單、`9`/`0` 移游標，其餘吃掉。**`5` 只在已定案時是 Delete**（未定案時它是輕聲），**`6` 則在任何狀態都是 Backspace**——推翻 2026-08-14 的「`5`/`6` 不能挪用」，代價是輕聲失去右手鏡像鍵。
- **閒置編輯層（2026-08-14）**：沒有組字串時**整排數字**代送編輯鍵——`1` Home、`2`/`3` Shift+Home/End、`4` End、`5` Delete、`6` Backspace、`7`/`8` ↑↓、`9`/`0` ←→（`1`-`4` 由中間往外讀：左兩個往左、右兩個往右，靠內的帶選取）。**中英模式共用同一層**（英文模式下組字串非空時數字＝字面數字，`user123` 打得完）。**只吃沒按 Shift 的數字**（Shift+9＝（、Shift+1＝！照舊）；**按著 Ctrl/Alt 一律不接手**。
- **`Tab`／`-`／`=` 不再是導航鍵（2026-08-14）**：Tab 不再是 Backspace（由 `6` 接手），`-`/`=` 不再是 Home/End（由 `1`/`2` 接手）也不再跳組字串頭尾。Tab 從此完全還給應用程式；`-`/`=` 見下一條。
- **中文模式的符號＝全形，但 `-` `=` `+` `` ` `` 例外（2026-08-14 立、2026-08-16／08-17 開例外）**：沒有對映的符號鍵（`@ # $ % & * |`）**閒置時也不輸出**，一律吃掉；要打就 Shift 切英文。`/`＝、（對齊小狼毫，與 `\` 同義但近得多）。**`-` `=` `+` 直接打出半形字元**（使用者裁示：要與小狼毫一致。Rime `punctuator/half_shape` 在同一套鍵位下就是原樣送出，中英混排時夠常見，為一個連接號切英文不划算）；行為與其他標點相同＝定案未定案音節、融入組字串、不 commit。`_`＝「——」不變。**`` ` `` 也是半形（2026-08-17）**，注音功能鍵移除後回歸普通標點；Shift 位的 `~` 維持全形 ～。
- 模態選字（2026-07-27 大改版）：組字中行內整句轉換；選單內 `1`-`6` 選字（每頁 6）、`7`/`8` 翻頁不環繞、其他鍵關窗並執行原功能；方向鍵在組字中吃掉無作用。**選定後游標跳過該詞段**（可連按 `8` 一路往右改）。
- **選字不動到其他字（2026-08-09）**：套用候選後，所選跨距以外被重走改掉的位置一律釘回原字。詞格的 `overrideCandidate` 會 reset 所有與所選跨距重疊的節點，不修就會發生「不鏽鋼[悲] 選『鋼杯』→ 鏽 變成 秀」。
- **選字記憶＝上下文硬性覆寫、改一次就生效（2026-08-09）**：記「在〈前一／前兩個字〉後面，讀音〈X〉打成〈值〉」，套用時對詞格下高分覆寫（不是加分數）。**無時間衰減**；新選的值一律排到第一名、對手各 −1。檔案 `%APPDATA%\MspyIME\user-choices.txt`。舊的 `UserPreferenceLM` 加分層與 session `UserOverrideModel` 整組移除。
- **Shift 單獨輕按＝中英切換（v5，2026-08-08：不 commit）**：有組字串時整段留著不上屏，切英文後打的字**直接長在同一個未 commit 組字串裡**（literal 讀音），Shift 切回中文可續打中文；只在 Enter／失焦／NumPad 上屏。英文模式的空白＝字面空白。**沒有組字串時純切模式、按鍵全放行、不補空白**。半形分隔空白只在組字串內部、依游標左邊的字自動補（中→英看中文字、英→中看英數）；v4 的被動記憶（`_lastCharClass`、游標快照、WH_MOUSE hook）已整組刪除。實作走 preserved key＋「鍵盤關閉時仍問 `IsVirtualKeyNeedMspyEnglish`，只有 composer 非空才吃鍵」。**中英模式每個應用程式各自記憶，新 app 一律英文**。
- **空白鍵＝定案，永不上屏（2026-08-02 立、2026-08-16 收掉最後一條 commit 路徑）**：有待定音節＝以一聲/輕聲預設轉成字、純注音則定案成注音符號（皆留在組字串）；**沒東西可定案時＝打出一個半形空白**，以 literal 讀音融入組字串、游標走得過、Backspace 刪得掉，**不 commit**。組字串裡什麼都沒有時回到閒置，不開只有空白的組字串。**上屏從此只有 Enter、失焦、以及組字中按 NumPad（先上屏再輸出數字）**。
- **Enter＝所見即所得（2026-08-17）**：上屏的就是 `composedText()`，也就是畫面上那一串。還顯示成注音的部分**以注音送出**，既不丟掉也不在送出瞬間換成字：`nc`→ㄋㄧㄠ（以前什麼都沒有）、`k`→ㄎ（以前是「科」）、`ni3hk`→你ㄏㄠ（以前是「你蒿」）。要字就先按空白鍵或聲調鍵。
- **Backspace／`6` 把還在打的音節整個刪掉（2026-08-17）**：一鍵的 ㄅ 與兩鍵的 ㄅㄧㄠ 都是按一次就全沒。**同日先做過「一次退一個鍵」再由使用者撤銷**（理由與實作見 docs/spec.md 的決策記錄，勿再實作）：一個音節是一次打完的單位，就該是一次刪掉的單位。
- **`` ` `` 的注音功能整組移除（2026-08-17）**：`hollowFinal_`／`feedHollowFinal`／`HollowFinalDisplay` 刪除，那顆鍵回歸半形 `` ` ``。打注音符號剩兩條路：空白鍵（音節無字時定案成符號）與 Enter（原樣送出）。**代價：單獨的韻母（ㄠ、ㄧㄝ）打不出來了**，使用者已知並接受。
- **標點＝定案並融入組字串，不 commit（2026-08-04）**：`,` 打完是「最，」仍在組字串裡、仍可選字；閒置打標點＝開新組字串。**無任何自動上屏界線**，只有 Enter／失焦／NumPad 會上屏（Esc 是整段丟棄）。
- 顯示名稱「阿特雙拼輸入法 vX.Y.Z」，zh-TW（LANGID 0x0404），輸出繁體。**版本號跟在名稱後面（2026-08-16）**：那一行就是 Windows 工作列圖示的滑鼠提示，也是「設定 → 語言」清單裡的名稱，滑過去就知道現在跑的是哪一版。字串由 `ime\SampleIME\SampleIME.vcxproj` 的 `MspyGenerateVersionHeader` target 從根目錄 `VERSION` 產生 `Version.h`（不進版控），**只有重新註冊 DLL 時才會寫進註冊表**——`install.ps1` 每次都會做，dev 註冊要重跑 `register-dev.ps1`。

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

### 發佈前一定要先查 Mac 版有沒有跟上

**使用者一說要 release，第一件事是跑 `python scripts\check-parity.py`，在打 tag 之前。**
打 tag 會觸發 macOS 的建置與發佈，所以那之後才發現沒對齊就太遲了。

- **回報「aligned」** → 照常進行。
- **回報有落差** → **停下來問使用者**，不要自作主張。用中文說清楚三件事：
  差在哪、補起來大概要動幾個檔案、以及兩個選項——(a) 先把 Mac 補齊再一起發，
  (b) 這次只發 Windows 版，Mac 版留待下一版。
  **等使用者回答再動作。**

判斷「還差多遠」的參考：`mac/docs/NOTES.md` 的〈Tracking upstream〉逐版記了實際成本，
v0.3 到 v0.6 分別是 1、1、2、1 個檔案。多數上游改動 Mac 完全不用動，
因為行為在共用的 `core\` 裡、`ArtBridge.mm` 只是轉手。真的要動時通常是兩類：
`mspy::Composer` 多了方法（補 `ArtBridge.mm` 一行）、或鍵路由變了（照著改
`-handleKeyDown:client:`，**順序有意義**）。

**只有 Windows 版的發佈是可以的**，但要是使用者選的，不是預設。

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

- 2026-08-18：**tag v0.8.2 並發佈 GitHub Release**（兩個資產：`art-shuangpin-v0.8.2.zip`、
  `art-shuangpin-mac-v0.8.2.zip`）。距 v0.8.1 共 11 個 commit，**詞庫仍是 `c07e7285…`**
  （`data/`／`engine/` 自 v0.7.1 零改動），`ime/` 與 `mac/src/` 也都沒動——
  全部改動都在共用的 `core/composer.cpp`。
  內容＝2026-08-16 的空白鍵不再 commit、2026-08-17 的 Enter 所見即所得／`6` 全狀態 Backspace／
  `` ` `` 退休，以及 08-17 當天先做後撤的「Backspace 逐鍵退」（最終版是整個音節一起刪）。
  **Release 內文的主軸是「上屏只有 Enter 一個入口，而且所見即所得」**——四項改動其實是同一件事的四個面向，
  照這條線敘述比逐項列清單好懂；每一項都把**代價**寫在正文裡（輕聲失去右手鏡像鍵、單獨韻母打不出來）。
  發佈前照規則跑 `check-parity.py`（**aligned**，因為每一輪改完當下就推進過 marker），
  打 tag 前確認 push 的 macOS CI 是 success。
  **順序照 v0.8.1 的教訓**：升 `VERSION` → 兩架構 `ctest`（各 175 全過）→ 走 `.sln` 重建兩個 DLL
  （確認字串是「阿特雙拼輸入法 v0.8.2」）→ `make-package.ps1`，zip 名稱與 DLL 內版本一致。
  check-tutorials 12 課全綠、check-drill-coverage 400＋11＝411。
  **本機已重裝**：先前測試用的 DLL 建於 `VERSION` 還是 0.8.1 的時候，行為是新的、提示裡的版本號卻是舊的，
  所以用打包出來的 `install.ps1` 重裝一次，兩個架構的 sha256 都與出貨產物相同。

- 2026-08-17：**注音階段的四項調整（使用者要求，尚未發佈，VERSION 仍 0.8.1）**。
  同一個抱怨的四個面向：**畫面上是什麼，就該能編輯什麼、就該送出什麼**。
  (a) **Backspace：先做「一次退一個鍵」，同日由使用者撤銷，改成整個音節一起消失**。
  原本的不一致是真的：拼不出字的音節（`nc`＝ㄋㄧㄠ，還留在 `pending_`）逐鍵退，
  而有一聲讀音的（`bc`＝ㄅㄧㄠ 已被 eager 插進詞格）整個消失。先做的版本是統一成逐鍵退
  （`Unsettled` 多存 `keys`、Backspace 先刪節點再把少最後一鍵的部分餵回 `pending_`），
  **使用者實測後裁示反過來統一**：一個音節是一次打完的單位，就該是一次刪掉的單位，
  打錯了重打兩鍵，不必記得游標停在音節的哪裡。最終版＝`pending_.clear()`，
  `Unsettled::keys` 與沒人用的 `SyllableInput::backspace()` 一併移除。
  決策記錄寫在 docs/spec.md 的 Backspace 條旁邊，含被撤銷版本的做法與理由，防止照同一套推理重做。
  (b) **`6` 在任何狀態都是 Backspace**：**推翻 2026-08-14** 那條「畫面還是注音時每個數字都是
  聲調鍵，`5` 與其鏡像 `6` 是輕聲僅有的兩個鍵，不能挪用」。已把代價講清楚後由使用者裁示：
  **輕聲從此只剩左手 `5`**，聲調鏡像表少一列。換來的是「刪除」不必判斷畫面是字還是注音。
  （看打練習從沒按過 `5`/`6`，教材零影響。）
  (c) **Enter＝所見即所得**：`takeCommitText()` 改成 `composedText()` ＋ `reset()`。
  還顯示成注音的部分以注音送出：`nc`→ㄋㄧㄠ（**以前什麼都沒有**）、`k`→ㄎ（以前是「科」）、
  `ni3hk`→你ㄏㄠ（以前是「你蒿」）。**問過使用者三個選項**，選的是最單純的「畫面是注音就送注音」，
  並知道 `d`+Enter 會從「的」變成 ㄉ。空白鍵與聲調鍵仍是「轉成字」的指令，Enter 只負責送出。
  (d) **`` ` `` 的注音功能整組移除**：`hollowFinal_`／`feedHollowFinal`／`HollowFinalDisplay` 刪除，
  那顆鍵回歸半形 `` ` ``（與 `-` `=` `+` 同類）。**Shift 位的 `~` 維持全形 ～**（另外問過）。
  **代價：單獨的韻母（ㄠ、ㄧㄝ）打不出來了**——原本靠 `` `k ``／`` `x ``；使用者知情並接受，
  因為剩下兩條路（空白鍵對無字音節、Enter 對任何注音）已涵蓋實際用途。
  **問問題的價值**：使用者舉的例子是 `nc` 與 `k`，而 `k` 現況會轉成「科」不是被丟掉——
  「只修被丟掉的」滿足不了它，非得改成 WYSIWYG 不可。這一點是先用 REPL 實測現況才問得出來。
  教學課程〈打注音符號本身〉整課重寫成那兩條路，`6` 鍵帽改成 ⌫，聲調鏡像表與速查表同步。
  驗證：core `ctest` x64／x86 各 **175 全過**（刪 3 個反引號測試、新增 2 個）、
  check-tutorials 12 課全綠、check-drill-coverage 400＋11＝411、
  **`web/drills.js` 重新產生後零 diff**（課文只在單鍵音節收尾按空白、從不按 `6`）、
  兩架構 IME DLL 重建。Mac 零改動（四項都在共用的 composer 裡），marker 只推進 commit。

- 2026-08-16：**空白鍵不再 commit，改成打出半形空白**（使用者要求，**尚未發佈**，VERSION 仍 0.8.1）。
  空白鍵原本兩種工作：有待定音節就定案，**沒東西可定案就整段上屏**。後者拿掉了——
  現在改成「打出一個普通的半形空白」，以 literal 讀音融入組字串，游標走得過、Backspace
  刪得掉、跟整段一起上屏。**要 commit 就按 Enter。**
  理由：標點自 v0.4、Shift 切換自 v0.5 就都不 commit 了，空白鍵是**最後一個會突然把整段
  送出去的可見字元**，偏偏又是最常按到的鍵。
  實作只有一處：`settleOrCommit()` 改名 `settleOrSpace()`，尾巴的 `takeCommitText()` 換成
  `insertLiteralText(" ")`。**唯一的例外是詞格空的時候**（裸 `` ` `` 剛被空白丟掉）——回到
  閒置，不開一段只有空白的組字串。閒置時的空白照舊放行給應用程式。
  **連帶查證**：`drill_gen` 只在「單鍵音節收尾」按空白（那時一定有東西可定案），所以
  `web/drills.js` 重新產生後**與原本位元組相同**；13 課看打練習不受影響。
  教學課程〈定案與上屏〉第 4 步之後重寫（空白 → 「中文␣」→ Backspace 示範它是一般內容 →
  Enter 上屏），課名改成〈定案與上屏：空白鍵是定案鍵〉。
  **順手修掉 spec 三處早就過期的敘述**：`ni3hk3` + 空白 + `9``9` 那幾列（空白當時已經會
  commit，游標鍵根本走不到）、`-` 跳頭尾（2026-08-14 就取消了）、以及**所有「上屏時機」
  清單都漏掉的 NumPad**（組字中按 NumPad 會先整段上屏再輸出數字，`_HandleNumpadCommit`）。
  正確的上屏路徑是 **Enter／失焦／NumPad** 三條。
  **Mac 零改動**：中文分支把空白和其他可見字元一樣交給 `wouldConsumeChar:`／`feedChar:`，
  英文分支另有自己的 literal space 路徑，兩個 body hash 都沒動。marker 只推進 commit。
  驗證：core `ctest` x64／x86 各 **176 全過**（改寫 1 個測試）、check-tutorials 12 課全綠、
  check-drill-coverage 400＋11＝411、`web/drills.js` 無 diff、兩架構 IME DLL 重建。

- 2026-08-16：**tag v0.8.1 並發佈 GitHub Release**（兩個資產：`art-shuangpin-v0.8.1.zip`、
  `art-shuangpin-mac-v0.8.1.zip`）。距 v0.8.0 共 2 個 commit，內容就是同日那兩件事
  （`-` `=` `+` 半形解禁、工作列提示帶版本號），**詞庫仍是 `c07e7285…`**。
  發佈前照規則跑 `check-parity.py`：報 1 個 commit 落差 → 讀過確認 `mac/src/` 零改動 →
  推進 marker → 再跑一次 aligned。打 tag 前確認 push 的 macOS CI 是 success。
  **打包前先升 `VERSION` 再建 DLL**（v0.8.0 那次撞名的教訓）：這次順序是
  升版 → 重建兩架構 → `make-package.ps1`，zip 名稱與 DLL 裡的版本字串一致。
  **建置的坑**：`make-package.ps1` 的預設來源是 `ime\x64\Release` 與 `ime\Release`，
  那是用 **`.sln`** 建才會產出的位置；直接對 `SampleIME.vcxproj` 下 msbuild 會掉到
  `ime\SampleIME\{x64\,}Release\`（`$(SolutionDir)` 退化成 `$(ProjectDir)`），
  打包會拿到舊 DLL。要打包就走 `.sln`（x86 的 Platform 名稱是 **Win32**，不是 x86）。
  **本機尚未重裝**：提示裡的版本號只在 `regsvr32` 重新註冊時才寫進註冊表，
  所以要看到「v0.8.1」必須跑 `install.ps1`（dev 註冊則是 `register-dev.ps1`）。

- 2026-08-16：**`-` `=` `+` 解禁成半形＋工作列提示帶版本號；VERSION 升到 0.8.1**。
  (a) **符號**：2026-08-14 的「中文模式只輸出全形」把 `@ # $ % & * | - = +` 全部吃掉，
  使用者要求把 `-` `=` `+` 放出來。查 `rime-prelude/punctuation.yaml` 的 `half_shape`：
  這三個鍵在小狼毫（同一套微軟雙拼鍵位）下就是**原樣送出半形**，`_` 則是 `——`（我們本來就有）。
  **使用者裁示採半形**（另一個選項是全形 `－＝＋`，會維持「只出全形」的原則但與小狼毫不同）。
  實作只是 `core/composer.cpp` 的 `DirectPunctuation` 多三個 case，所以行為與其他標點完全一致：
  定案未定案音節、以 literal 讀音融入組字串、**不 commit**。`@ # $ % & * |` 維持吃掉。
  **提醒**：數字排在中文模式仍然是聲調鍵／編輯鍵，所以整條算式（`1+1=2`）還是打不出來——
  文件已標明，別把「算式」寫成解禁的理由。
  (b) **工作列提示顯示版本號**：那一行是 TSF 的 profile description（同時也是「設定 → 語言」
  清單的名稱），改成 `阿特雙拼輸入法 v0.8.1`。`VERSION` 仍是唯一來源：新增
  `SampleIME.vcxproj` 的 `MspyGenerateVersionHeader` target，用純 MSBuild
  （`System.IO.File::ReadAllText` ＋ `WriteLinesToFile WriteOnlyWhenDifferent`）產生
  `ime/SampleIME/Version.h`，**不進版控**（已加 .gitignore），版本沒變就不會逼重編。
  **坑：字串只在重新註冊 DLL 時才寫進註冊表**——`install.ps1` 每次都會 `regsvr32`，
  但只換 DLL 的 `deploy-dev.ps1` 不會，提示會停在舊版本號。
  (c) **Mac 對齊**：`check-parity.py` 報 1 個 commit 落差，讀過後**確認 `mac/src/` 零改動**——
  `-handleKeyDown:` 把每個可見字元交給 `wouldConsumeChar:`／`feedChar:`，
  閒置編輯層只認數字排，所以三個鍵原封不動走到共用的 composer。marker 已推進到 `091aced`＋0.8.1。
  版本號則刻意**不**加到 macOS 的輸入來源名稱：那個名字來自 `Info.plist`，
  裡面的 `CFBundleShortVersionString` 已經是同一份 `VERSION`。
  順手修掉 `mac/CLAUDE.md` 兩處過期敘述（「`-`/`=` 送 ⌘←/⌘→」、測試數 171/173 → 174/176）。
  (d) 驗證：core `ctest` x64／x86 各 **176 全過**（改了 4 個測試：`-`/`=` 不再是 no-op、
  選單中按 `-` 會關窗並打字、半形黑名單少三個鍵）、兩架構 IME DLL 重建、
  check-tutorials 12 課全綠、check-drill-coverage 400＋11＝411、check-parity aligned。

- 2026-08-14：**tag v0.8.0 並發佈 GitHub Release**（兩個資產：`art-shuangpin-v0.8.0.zip`、
  `art-shuangpin-mac-v0.8.0.zip`）。距 v0.7.1 共 12 個 commit，**詞庫一個位元組都沒變**
  （`out/data.txt` sha256 仍是 `c07e7285…`，`data/` 與 `engine/` 自 v0.7.1 零改動），
  內容純粹是按鍵行為：閒置編輯層、Tab／`-`／`=` 交還、`5`/`6` 組字中可刪、中文模式只出全形、
  `/`＝、、教學網站的 `Alt`+`R`／`Alt`+`N` 與兩處讀音修正，外加 macOS 的 os_log 修正。
  發佈前照規則先跑 `check-parity.py`（aligned），並確認**打 tag 前最後一次 push 的 macOS CI 是
  success**——這輪改了 3 個 `mac/src/` 檔案而本機編不了，那個 run 是唯一的檢驗。
  驗證：x64／x86 `ctest` 各 176 全過、check-drill-coverage 400＋11＝411、check-tutorials 12 課全綠。
  **使用者已在真機實測**（先前那包名為 v0.7.1、內容其實是 reorder 後建置的 zip）：
  確認 `1`/`4` 只跳到底、`2`/`3` 帶選取，且合成 Shift 沒有誤觸中英切換。
  `release-mac.yml` 建的是 **draft**，Windows zip 由本機 `make-package.ps1` 打包後上傳同一個 release，
  填好中文內文才 `--draft=false` 發佈。
  **命名的坑**：`make-package.ps1` 用 `VERSION` 命名 zip，所以在 `VERSION` 升版之前打的包會與
  已發佈的舊版**撞名但內容不同**——這次就讓使用者裝到一個叫 v0.7.1、其實是 v0.8.0 行為的檔案。
  要在打包前先升 `VERSION`，或至少提醒。

- 2026-08-14：**閒置編輯層的第二輪調整＋「那樣」讀音修正**（同日回饋）。
  (a) **`1`-`4` 重排**：`1` 行首、`2` 選到行首、`3` 選到行尾、`4` 行尾——由中間往外讀，
  左兩個往左、右兩個往右，靠內的那個帶選取（原本是 1/2 移動、3/4 選取）。
  (b) **`5`/`6` 在組字中也能刪**：新增 `Composer::feedDelete()`（`deleteReadingAfterCursor`），
  `feedChar` 的數字分支在**已定案**狀態把 `5` 接到它、`6` 接到 `feedBackspace()`。
  **只在已定案**——畫面上還是注音時每個數字都是聲調鍵，而 `5` 與其鏡像 `6` 是**輕聲僅有的兩個鍵**，
  不能挪用（查過教材：`5`/`6` 在 13 課看打練習裡一次都沒被按過，所以沒有連帶損失）。
  `feedDelete` 記進 `mac/parity-allow.txt`：兩邊都走 `feedChar('5')`，bridge 不需要它；
  **真正的 Delete 鍵在組字中仍然放行給應用程式**，兩邊都是，要修就一起修。
  (c) **看打練習的 `Alt`+`R`／`Alt`+`N`**：原本裸 `R`／`N` 只有練完才吃鍵，因為練習中每個裸鍵都屬於課文；
  但「從頭再來」最需要的時機正是**練到一半發現打壞了**。改成 `Alt`+`R` 隨時可重來、`Alt`+`N` 練完才換課。
  選 Alt 是因為 Ctrl+R 會重新整理（真的觸發就前功盡棄），用 `event.code` 而非 `event.key`
  （macOS Option+R 在某些配置會打出 ®）。快捷鍵的判斷移到 keydown 監聽器的修飾鍵防護**之前**。
  (d) **「那樣」的讀音（使用者回報）**：補完課文出現 `那樣/ㄋㄚˇ-ㄧㄤˋ`。詞庫本身是對的
  （`ㄋㄚˋ-ㄧㄤˋ 那樣`、`ㄋㄚˇ-ㄌㄧˇ 哪裡`、`ㄋㄚˋ-ㄌㄧˇ 那裡` 都在），但它**也收了變體
  `ㄋㄚˇ-ㄧㄤˋ 那樣`**，貪婪集合覆蓋為了補 ㄋㄚˇ 這個鍵位就挑了三聲那一個。
  使用者裁示：**三聲 ㄋㄚˇ 是「哪」，「那樣」的那讀四聲**。修法是把兩個讀音都用手寫課文釘死，
  並把「那樣」寫進 `drills\avoid-words.txt`——手寫課文已覆蓋 ㄋㄚˇ 之後補完就不會再碰它。
  **課文放在〈八、綜合練習〉的最後一句**（同日回饋：原本放〈九、收尾〉的兩行讓那一課變成 7 句、
  全檔最長，而八只有 3 句）：`不要 那樣/ㄋㄚˋ-ㄧㄤˋ 說 ， 你 想 去 哪裡/ㄋㄚˇ-ㄌㄧˇ 都 可以 。`
  **合併成一句是刻意的**——這兩句的重點就是 ㄋㄚˇ／ㄋㄚˋ 的對照，拆到不同課就看不出來了。
  **坑**：`drills\lessons.txt` 的格式是**一行一句、結尾只能有一個 。！？**（檔頭有寫），
  所以「你想去哪裡？不要那樣講。」串成一行會被 drill_gen 擋下來
  （`hand-written line does not convert cleanly`）；要放同一行就得用**逗號**接成一個句子。
  驗證：core `ctest` x64／x86 各 **176 全過**；兩架構 IME DLL 重建；check-drill-coverage 400＋11＝411、
  check-tutorials 12 課全綠、check-parity aligned；Chrome 實測 **13 課看打練習全部重播、輸出全對、零失誤**，
  且裸 `R` 不重來、`Alt`+`R` 重來、`Alt`+`N` 只在練完才換課。

- 2026-08-14：**閒置編輯層取代零散的導航鍵；Tab／`-`／`=` 還給應用程式；中文模式只輸出全形**。
  使用者要求把 `+`／`-`／Tab 的功能取消，改成「沒有組字串時整排數字＝編輯鍵」，並且中英模式共用。
  **決策全部先問過再動手**（四輪提問）：生效範圍只在**完全閒置**（組字中維持 `8` 開選單、`9`/`0` 移游標，
  原本 `-`/`=` 的跳頭尾**直接取消不補**）；Shift+數字**維持現況**照舊出符號；英文模式下組字串非空時
  數字＝字面數字（`user123` 一路打得完）；**不做打數字的組合鍵**——查證後回報 **Fn 鍵作業系統看不到**
  （鍵盤微控制器直接處理，例外只有 Lenovo BIOS 與 Apple），且「Fn＋數字排」不是通用做法（內嵌數字鍵盤
  疊在 `U I O J K L M` 那一區，13-14" 機型多半已取消），使用者知情後仍選擇維持。
  **對映**：`1` Home、`2` End、`3`/`4` Shift+Home/End、`5` Delete、`6` Backspace、`7`/`8` ↑↓、`9`/`0` ←→。
  **實作中被逼出來的必要修正**：中文模式的按鍵路由**原本沒有檢查 Ctrl/Alt**——既有設計靠「沒對映就放行」
  意外擋住了它，一旦改成「一律吃掉剩下的可見字元」，`Ctrl`+`A` 會被吞掉、`Ctrl`+`1` 會變成 Home。
  已在兩個路由函式最前面補上「按著 Ctrl 或 Alt 就整個不接手」，並讓 `wouldConsume` 拒絕非可見 ASCII
  （macOS 那半本來就有 `hasCommandLike`，Windows 這半漏了）。
  **3/4 注入 Shift 不會誤觸中英切換**：中間那個 Home/End 會走 `UpdateModifiers` 的 default 分支清掉
  `IsShiftKeyDownOnly`，`CheckShiftKeyOnly` 就不放行 preserved key（讀碼確認，仍列為實測項目）。
  **符號另外問了一輪**：使用者要求「恢復成小狼毫微軟雙拼的符號輸入狀態」。查 Rime 官方
  `rime-prelude/symbols.yaml` 並確認 `double_pinyin_mspy.schema.yaml` 的 `full_shape`／`ascii_punct`
  都沒寫 `reset:`（＝預設半角＋中文標點），逐鍵比對後**只有五處不同**：`/`→、、`|`→·、`$`→￥、
  `~`（Rime 給半形）、`` ` ``（我們拿來當注音功能鍵）。裁示結果：**`` ` `` 保留功能鍵**、**只加 `/`→、**
  （`\` 與 `/` 在 Rime 都是 、，`/` 近得多）、**標點候選清單不做**、其餘維持。並確立
  **「中文模式一律只輸出全形」**——沒有全形對映的符號鍵（`@ # $ % & * | - = +`）從此**閒置時也不輸出**
  （原本閒置會漏出半形），與「數字排禁用」同一個取捨。
  **看打練習第七課的頓號改教 `/`**（使用者中途回報）：`drill_gen` 的 `PunctuationKey` 反查表
  `、` 從 `\` 改成 `/`，課文 `#intro` 一併改寫（順帶修掉「問號 /」現在會誤導的寫法）。
  **macOS 同步**：`ArtNavigation` 的 enum 從 5 個擴成 10 個（新增 ↑↓、forward delete、選取到行首/行尾），
  `-handleKeyDown:` 與英文分支共用新的 `-injectIdleEditingKeyIfWanted:`，`upstream-alignment.txt` 兩個
  body hash 已更新。**順帶退休一個修不好的缺陷**：2026-08-14 稍早記的「Mac 閒置 Tab 在 Chromium 系
  程式會跳走焦點」——Tab 不再被攔截，問題不存在了；量測記錄保留在 `mac/docs/NOTES.md` 第 6 條，
  並註明「未來若想把 Tab 拿回來，這就是代價」。
  驗證：core `ctest` **174 全過**（新增 3 個：半形符號閒置也吃掉、控制字元不吃、`/`＝、）；
  x64／x86 IME DLL 皆重建成功；check-drill-coverage 400＋11＝411 全覆蓋；check-tutorials 12 課全綠。
  `web/drills.js` 只差頓號那一鍵——`MoveCursorTo` 的 `-`/`=` 捷徑實際上從沒被選中過，移除後產物與原本相同。

- 2026-08-14：**Mac 的閒置 Tab 在 Chromium 系程式失效——查到根因、決定不修、只寫進文件**。
  使用者回報 Mac 上 Tab 刪不掉字。逐步縮小：組字中 Tab 正常、閒置 9/0 正常、**文字編輯（TextEdit）
  閒置 Tab 也正常**，只有 Slack／LINE／VS Code／Chrome 不行且**焦點會跳走**。
  我最初推論「Tab 沒送達輸入法」，**錯了**——加了按鍵層 log 實測，證據推翻推論：
  Tab 有到（`keyCode=48`）、被吃掉、`CGEventPost` 也成功（`injected=1`）、注入的 Backspace
  2.4ms 後回到輸入法並被放行（`keyCode=51`），**291ms 後 `activateServer` 被重新呼叫＝焦點仍然跳走**。
  真因：**Chromium 在輸入法回傳 YES 之後仍自行執行焦點導航**——它把「消耗了按鍵卻沒產生文字或
  marked text」視為未使用。AppKit host 尊重同一個回傳值，所以文字編輯正常；Windows 完全看不到
  這個問題，因為 TSF 的 keystroke sink 排在應用程式之前。
  **範圍比想像小**：組字中的 Tab 在所有程式都正常（那時有 marked text，Chromium 會尊重），
  壞掉的只有「刪已上屏的字」，而真正的 Backspace 能做同一件事。
  **唯一的修法是 CGEventTap**（在 host 之前攔 Tab）。使用者一度決定要做，評估後改為不做：
  那是為了一個鍵在一種情境而裝的全域攔截器，啟停條件只要有一個情境沒想到，使用者就會在整個系統
  失去 Tab。決策與 log 時序記在 `mac/docs/NOTES.md`〈What to check first〉第 6 條，
  使用者向的說明在 `docs/guide.md` §11 與 `mac/docs/INSTALL.md`。
  **順帶修掉一個真正的缺陷**：`ArtLog` 走 `NSLog(@"...%@", msg)`，而統一日誌系統會把 `%@` 的參數
  塗成 `<private>`——Console.app 看得到行、看不到內容。**三個「永遠會印」的訊息**（詞庫載入失敗、
  Accessibility 授權失效、IMKServer 起不來）因此從來沒人讀得到。改用
  `os_log(..., "%{public}s", ...)`（唯一的開關就是格式指示字，沒有 defaults key 可調），
  並新增不看 debug 旗標的 `ArtLogAlways` 給那三處用；debug 開啟時會先印一行
  `debug logging is ON`，「到底有沒有生效」因此可以從 log 本身回答。
  **CI 也改了**：`mac.yml` 建完後用 `ditto` 打包並 upload-artifact（14 天）。
  在此之前，要試一個 `mac/src` 的改動只能打 tag，而**打 tag 等於發佈**。
  按鍵層的診斷 log 用完即還原，os_log 修正與 CI 產物保留。

- 2026-08-14：**tag v0.7.1 並發佈 GitHub Release**（兩個資產：`art-shuangpin-v0.7.1.zip`、
  `art-shuangpin-mac-v0.7.1.zip`）。距 v0.7.0 共 5 個 commit，**程式碼一行沒動**——
  `core/`／`engine/`／`ime/`／`mac/src/` 與 v0.7.0 相同，內容是詞庫長度加權修正、
  `對齊` 的 postprocess 提升、看打練習的「麼」改單鍵，以及 PROVENANCE 與 .gitignore。
  發佈前照規則先跑 `check-parity.py`：回報落後兩個 commit，但查證後那兩個只動 `data/`
  與教材腳本（`git diff --name-only <marker>..HEAD -- core/ engine/ cli/ docs/spec.md` 為空），
  而 `release-mac.yml` 自己跑 `build-data.sh` 從同一份 `data/` 建詞庫，**mac/src/ 零改動**，
  故補齊＝推進 `mac/upstream-alignment.txt` 的 commit／version 兩行。使用者裁示補齊後兩平台一起發。
  驗證：`build-data.ps1` 重跑產物 sha256 不變（`c07e7285…`）、x64 與 x86 `ctest` 各 173 全過、
  check-drill-coverage 400＋11＝411 全覆蓋、check-tutorials 12 課全綠。兩個架構的 IME DLL 以
  msbuild 重建後 `make-package.ps1` 打包。**Release 內文的敘事重點是「只換詞庫但值得升級」**：
  用「對齊 vs 對＋其」的分數對照講清楚為什麼多字詞會輸，並附 87.5%→93.1% 的量化表。
  **本機已重裝**：原本裝的 DLL 建於 2026-08-13 15:04，而 `0d4b2a9`（撤銷母音式拼法）的 commit
  時間是 15:13——差 9 分鐘，光看時間戳斷不出它是撤銷前還是撤銷後的建置（本專案常「先建置測試、
  後 commit」），所以直接以管理員跑打包出來的 `install.ps1` 蓋掉。三個檔案的 sha256 現在都與
  v0.7.1 的產物相同。x64 的舊 DLL 被執行中的 TSF 宿主鎖住，照既定升級路徑停放為
  `ArtShuangpin.dll.old.87bc337e`，重開機後可刪；**已載入輸入法的應用程式要重開**才會吃到新 DLL。

- 2026-08-14：**看打練習的「麼」改用單鍵 `m`＋空白（推翻 2026-08-10 的讀音裁示）**。
  使用者裁示：**教學規則就是單鍵優先**，「這跟省不省鍵沒有關係，主要是強調單音節的習慣性；
  為此而特地記一個肌肉例外，不划算」。原本〈單鍵音節〉課的「什麼」與另一課的「怎麼」
  釘的是教育部標準 `ㄇㄜ˙`，而單鍵 `m` ＝ **ㄇㄛ**（ㄅㄆㄇㄈ 呼名帶 ㄛ），拼不出 ㄇㄜ，
  所以產生器只能打 `m`+`e`——**同一個字在課程裡有兩種打法**，正是要避免的例外。
  改法：`drills\lessons.txt` 兩處 `ㄇㄜ˙` → `ㄇㄛ˙`（詞庫兩種讀音都收「什麼／怎麼」，
  分數同級，句子照樣乾淨轉換），現在兩處都打成 `m`＋空白。
  **連帶**：`me`（ㄇㄜ）失去唯一可讀的字，寫進 `drills\skip-syllables.txt` 並說明理由——
  這是該檔第二個「讀音問題」的條目，而且與 ㄓㄟ 那條**方向相反**：ㄓㄟ 是為了讀音正確而不練，
  ㄇㄜ 是為了打法一致而不練。覆蓋數 401＋10 → 400＋11，仍 411 全覆蓋。
  補完課文因為 ㄇㄛ˙ 已被課程覆蓋而重新配置（「那麼」讓位給「那樣／配額／檸檬」）。
  注意：畫面上不會出現錯誤讀音——單鍵 `m` 的未定案顯示只有 ㄇ，空白一按就是「麼」。

- 2026-08-14：**詞庫長度加權修正——整句轉換品質的最大單一改善**。使用者回報「對齊」永遠打成「對其」，
  要開選單挑第一個候選。根因不是「對齊」不在詞庫，而是 **unigram 模型的加總比較**：
  對齊 −6.3640（語料只出現 8 次）輸給 對 −2.6914 ＋ 其 −2.9019 ＝ −5.5933。
  上游自己的 `data/curation/analyzers/find_cover_issues.py` 就是在數這件事：**22980 個多字詞（15.8%）
  永遠當不了首選**。查下去發現真正的病灶在 `curation/builders/frequency_builder.py`：
  加權寫的是 `fscale ** (len(k) / 3 - 1)`，那個 `/3` 是 **Python 2 數 UTF-8 位元組**的遺跡
  （一個漢字 3 bytes → 單字 `3/3-1=0`、雙字詞 `6/3-1=1`＝×2.7 的長詞加分）。改跑 Python 3 之後
  `len()` 數的是字元，單字變成 `1/3-1=-0.667`、雙字詞 `-0.333`——**長詞加分變成長詞扣分**。
  已 `curl` 對過 **上游 master 早已修成 `len(k) - 1`**（本專案 vendor 的是修正前的快照 ee9941a6），
  所以這是補上游的修，不是自創偏離。
  **量化驗證**：抽 22010 個「詞庫裡該讀音的首選多字詞」，單獨打那串讀音會不會就出這個詞——
  **87.5% → 93.1%（修好 1224 個，弄壞 0 個）**。真實散文那一側是中性的：用 `drills/lessons.txt`
  重跑 drill_gen，步數與開選單修字的次數完全相同，`web/drills.js` 只差兩個「下一鍵按下去就會被改掉」
  的中間畫面（是→市、明→銘）。上游 `Postprocess.txt` 的 12 條 `promote-over-single-syllables`
  有 **10 條自動變成多餘**（沒事 好險 依舊 中醫 西醫 各式 試著 視野 步道 面試，postprocess 會硬性
  報 `no need to promote`），已註解掉並保留其 `assert` 當回歸測試；`before` 行同步更新成修正後的斷詞。
  **「對齊」修正後仍然不夠**（−6.2714 vs −5.9833），所以另外加了本專案第一條
  `promote-over-single-syllables 對齊 ㄉㄨㄟˋ-ㄑㄧˊ`，附 assert 釘住 `對-其他` 與
  `請-將-文字-對齊` 不被動到。
  **連帶要動的一處**：`scripts/make-filler-lessons.py` 的 `MIN_CHAR_SCORE` 從 −6.3 放寬到 **−6.5**。
  修正把所有單字分數往下推約 0.19，ㄉㄧㄚ 唯一可用的「嗲」掉到 −6.324 而被濾掉，看打練習少掉一個鍵位。
  重新量過兩組校準字仍乾淨分開（可接受的最低 嗲 −6.32、不可接受的最高 衲 −6.65），
  放寬後 `drills/filler.txt` 與修正前**完全相同**。
  驗證：`ctest` 173 全過、check-tutorials 12 課全綠、check-drill-coverage 401＋10＝411 全覆蓋。
  **已於 v0.7.1 發佈時部署**：`C:\Program Files\ArtShuangpin\{x64,x86}\mspy-data.txt` 的 sha256
  已與 `out\data.txt` 相同（`c07e7285…`）。

- 2026-08-13：**併入 macOS 版，一個 repo 管兩個平台**。原本 `D:\Claude\ArtMac`（private repo
  `weiwei84530/art-shuangpin-mac`）是 macOS 的 InputMethodKit 外殼，透過一個唯讀鏡像 clone
  （`vendor/art-shuangpin`）取用本專案的 `core\`／`engine\`。**合併的動機是帳密**：那台 Mac 是公司電腦，
  而唯一需要 GitHub 認證的東西就是「shell repo 是 private」——`vendor/` 抓的核心與詞庫本來就公開免認證。
  合併後那條路徑整段消失。
  **前置查證（決定要不要做的依據）**：`diff -rq` 比對鏡像與本專案的 `core/`、`engine/`，**零差異**——
  ArtMac 編的就是這裡的同一批 `.cpp`，所以沒有任何重複邏輯要調解；ArtMac 本身只有 33 個受控檔案、
  320 KB、11 個 commit、零二進位檔（選單列圖示是 build 時畫的）。
  **刪掉的比加上的多**：鏡像 clone、`vendor.pin`、sha256 釘選、`sync_art.py`、`make_transfer_zip.py`、
  `bootstrap.command` 的認證路徑（191→123 行）、以及 `tools/artprobe.cpp`（它存在的兩個前提——
  `repl.cpp` 含 `<windows.h>` 且 vendor 唯讀——同時消失，改用 `../cli/repl.cpp`）。
  **六個 commit**：(1) 路徑限定的 `.gitattributes`；(2) `CMakeLists` 依平台選 `MemoryMappedFile` 測試
  ＋ `repl.cpp`／`drill_gen.cpp` 改雙進入點；(3) `scripts/build-data.sh`；(4) 匯入 `mac/`；
  (5) 拆掉 vendor 改寫 Makefile；(6) macOS CI ＋ `check-parity.py`；(7) 離線 release zip；(8) 文件。
  **實作中踩到、值得記住的五個坑**：
  (a) **`core.filemode=false`（兩邊都是）**：複製檔案時 6 個 `.command`／`.sh` 的執行位元會**靜默消失**，
  `git add` 不會從磁碟撿。必須 `git update-index --chmod=+x`，否則 CI 會把 0644 的 `install.command`
  包進 zip，重現「您沒有適當的存取權限」對話框。匯入用 `git archive | tar` 並以 **blob hash 逐檔比對**
  驗證（31 個檔案全部相同）。
  (b) **`repl.cpp` 的 `wmain` 不能拆掉**：`check-tutorials.mjs:83` 是把**注音當 argv** 傳進去
  （`--shortest ㄉㄜ ㄏㄠ`），改成 `main(int, char**)` 會讓那些字在 Windows 上以 cp950 進來、
  `--shortest` 對每個讀音靜默回答 `-`。所以是「`Run(vector<string>)` ＋ 兩個進入點」而不是換掉。
  (c) **MSYS 的 `grep` 會在比對前吃掉 CR**：`build-data.sh` 第一版用 `if grep -q $'\r'` 當 CRLF 修正的
  守衛，於是**永遠不觸發**，直接出貨了一份每行帶 CR 的詞庫（175686 行）。改用 Python 的 bytes replace
  （與 PowerShell 版的 `.Replace("\r\n","\n")` 等價）。**順帶得到計劃預期不到的驗證**：`PYTHON=python`
  在 Windows 上跑 `build-data.sh`，產物與 `build-data.ps1` **位元組相同**，sha256 `1f484ca2…`＝v0.6 出貨的那份。
  (d) **`mac/Makefile` 的物件路徑**：來源變成 `../engine/...` 之後，`$(BUILD)/%.o` 會產生
  `build/../engine/X.o`，`mkdir -p` 把它解析成**真正的共用原始碼目錄**，`.o` 檔會掉進 `core/`、`engine/`
  並出現在 Windows 端的 `git status`。改用 `patsubst` 重寫物件名。
  (e) **`.gitignore` 的 `Release/` 會吃掉 `mac/release/`**：Windows 上 git 的忽略比對**不分大小寫**，
  那個給 MSVC 產物用的規則把整個目錄藏起來了——那些檔案正是要被打包進 release zip 的。加了
  `!/mac/release/**` 例外。
  **`check-parity.py` 的設計要點**：最強的一項是把 `IsVirtualKeyNeedMspy` 與
  `IsVirtualKeyNeedMspyEnglish` 的 **body 雜湊**記在 `mac/upstream-alignment.txt`——那兩個函式被
  `-handleKeyDown:client:` 逐條音譯且順序有意義，工具查不出語意漂移，但 hash 一變就是「去讀它」的
  零漏報訊號。**以函式名定位而非行號**（ArtMac 註解寫的 1597 是呼叫點，定義在 1600）。
  `--fix` 只修衍生物（Makefile 來源清單、版本號），**不得**改 ObjC++、不得重排鍵路由順序、
  不得推進對齊標記——已用注入漂移的方式測過它做什麼與不做什麼。
  **Release 形式**：macOS 走 GitHub Actions（`macos-15`，與 guard 同一個映像）建**通用二進位**、
  ad-hoc 簽章、`ditto` 打包成離線 zip，使用者只要下載→解壓→**右鍵打開** `install.command`。
  安裝腳本負責 `xattr -dr com.apple.quarantine`（下載來的 ad-hoc 簽章 app 不清會被說成「已損毀」）
  與**無條件** `tccutil reset Accessibility`（每一版簽章都不同，System Settings 那一列即使打著勾也已失效；
  這只影響閒置編輯層的數字排，輸入法本體不受影響）。Windows 那半維持本機手動打包，上傳到同一個 Release。
  **歷史保險**：刪除 private repo 前已 `git bundle create` 全歷史到 `D:\Claude\artmac-history-backup.bundle`
  （匯入 commit 引用的 `12b9c7d` 在 repo 刪掉後就解析不出來了）。**確認 Mac 能從合併後的樹建置並執行之前，
  不要刪那個 repo。**

- 2026-08-13：**看打練習六項回饋＋一個實作後撤銷的決策**。
  (a) **ㄧ／ㄨ 音節的「母音式」拼法：實作後同日撤銷，勿再實作。**使用者打「也」的直覺是
  ㄧ＋ㄝ＝`yx`，並（引用 Gemini 的說法）認為那就是微軟雙拼的打法。曾完整實作「零聲母的
  ㄧ／ㄨ 音節也可以用它接在聲母後面時的韻母鍵」共 13 組（`yw yx yc yq ym yd`、
  `ww wy wv wr wp wd ws`，原本全是死鍵），含 `YodMedialZhuyin`／`WauMedialZhuyin` 兩張
  分離的表與 `IsAlternateKeyPair()`（讓 `KeysForSyllable` 跳過，看打練習才不會改教 `yc`／`wv`）。
  **撤銷原因＝前提是錯的**：查 Rime 官方 `rime-double-pinyin/double_pinyin_mspy.schema.yaml`
  的 algebra，`ie$/X/` 要求結尾 literal 是 "ie"，而「也」的拼音是 `ye`（y-e 不是 i-e），
  沒有任何規則命中，所以**微軟雙拼的「也」就是 `ye`，`yx` 在微軟雙拼下打不出東西**；
  同一份 schema 逐條驗過 `ya yb yj yh yr yt yp ys yn y; wz wl wj wf wh wg` 全部與本專案現行一致。
  使用者自己切回小狼毫實測 `yx` 無輸出後裁示撤銷、改進自己的習慣。**要查微軟雙拼的實際行為，
  就查那份 schema 的 algebra 並手動推導一次，不要問別的模型也不要憑印象。**
  (b) **第九課兩處讀音**：「這個」原本被拆成 `這/ㄓㄟˋ` ＋ `個`（ㄍㄜˋ，四聲）兩個 token，
  改為釘死 `這個/ㄓㄜˋ-ㄍㄜ˙`；曾試圖把 ㄓㄟ 移到「這次」保住鍵位覆蓋，使用者同樣指正
  （教育部標準 這個／這次／這樣 的「這」都讀 ㄓㄜˋ），故 **ㄓㄟ 整個移進 `drills\skip-syllables.txt`**——
  這是該檔第一個「不是生僻字、而是讀音」的條目。覆蓋數 402＋9 → 401＋10，仍 411 全覆蓋。
  (c) **「跩」取代「拽」**（ㄓㄨㄞˋ→ㄓㄨㄞˇ，`vy4`→`vy3`）：使用者回報「他走路很拽」是對岸用法。
  (d) **練完一課用鍵盤繼續**：`R` 重來、`N` 下一課（最後一課不給 `N`，收尾訊息也不提）。
  判定在 `drill.shortcut()`，只有 `finished` 時才吃鍵，練習中的 R／N 仍算打錯。
  N 走 `selectDrill()`，與側邊欄點擊同一條路徑，選取狀態才會跟著移動。
  (e) **鍵盤三段式切換**（鍵盤下方按鈕）：提示下一鍵／不提示鍵位／隱藏鍵盤，記在
  `localStorage['drillKeyboard']`。**只在看打練習出現**——教學課程的亮鍵是課文本身不是提示。
  mode 1 只擋 `showHint()` 的鍵帽高亮（打對時的 `pressed` 動畫與「下一鍵：X」那行文字保留）。
  按鈕文字是**當前模式**、tooltip 才是「點一下的結果」（使用者回報原本的寫法會誤會）。
  **坑**：按鈕在 `.kb-stage` 裡，而 stage 的 pointerdown 會 `setPointerCapture` 做旋轉拖曳、
  把 click 吃掉——pointerdown 要先擋掉 `.kb-foot` 內的目標。
  (f) **提示要講清楚是哪隻手**：需要 Shift 的符號**只亮對側的 Shift**（`?` 在右手 → 亮左 Shift），
  空白鍵**只亮遠離前一鍵的那半邊**（前一鍵左手 → 右半邊亮，CSS 用 `linear-gradient(90deg,…)` 切半）。
  `LEFT_HAND` 與 `cli\keystrokes.h` 的 `IsLeftHand` 同一組字母，兩邊對「哪隻手在忙」的判斷才一致。
  (g) **記事本首行莫名縮排**（使用者截圖回報）：`.np-body` 是 `white-space: pre-wrap`，
  於是 **index.html 自己的換行與縮排也被畫出來**——開頭多一個空行＋十格縮排，結尾多兩個空行
  （所以文字看起來被推到框中間）。修法是把 `pre-wrap` 從容器移到 `#committed`（唯一
  textContent 含 `
` 的元素），容器回到 normal，原始碼縮排就照一般規則被收掉；
  這比「把 HTML 擠成一行」耐得住日後重排版。組字串的 `.ch` 沒有任何空白字元，已確認安全。
  **建置的坑（安裝後才發現）**：32 位元 IME 連的是 `build32\Release\mspy_core.lib`，
  只重建 x64 的 CMake 目標時 msbuild 會判定 Win32 專案「已是最新」而不重連結，
  `install.ps1` 的「檔案相同就跳過複製」再放行一次——**兩層各自合理的最佳化疊起來＝靜默裝到舊的**。
  改 core 之後 **`build` 與 `build32` 都要重建**，裝完用 `Get-FileHash` 對一次。
  驗證：x64 `ctest` 173、x86 core 75 全過；check-drill-coverage 411 全覆蓋、check-tutorials 12 課全綠；
  Chrome 實測無 console 錯誤；兩個架構的 DLL 已重建並安裝。

- 2026-08-12：**tag v0.6 並發佈 GitHub Release**（附 art-shuangpin-v0.6.zip）。距 v0.5 共 26 個 commit，
  其中只有 3 個動到輸入法本體（單鍵音節擴及全部 26 個字母、選字不動到其他字、選字記憶改成永久上下文覆寫），
  其餘 23 個是教學網站、看打練習、`docs/guide.md` 手冊與稽核工具——**這些隨 push 就由 GitHub Pages 上線了，
  不在 zip 裡**，Release 內文因此以三項本體改動為主軸、教材只做導引。發佈前重新建置兩個架構的 DLL
  並跑 `ctest`（173 全過）。**發佈前查到的狀態落差**：本機 `C:\Program Files\ArtShuangpin` 裝的是
  `v0.5-8-g0eebb7f` 的建置，SHA 與專案內產物相同，且 `0eebb7f..main` 完全沒動 `core/`／`ime/`／`engine/`／`data/`，
  故已安裝版本行為上已等同 v0.6。**驗證 DLL 是否含某次改動的辦法**（建置時間看起來比 commit 時間早時很有用，
  那通常只是「先建置測試、後 commit」）：`tr -d '\0' < X.dll | grep -o 'user-choices.txt'`——
  UTF-16 字串夾 null，去掉 null 再 grep 就搜得到。

- 2026-08-10：**動畫教學三項互動調整**（使用者回饋）。(1) **按過的鍵持續高亮**：`.pressed` 只有 255ms，
  太快，不認得鍵的人根本找不到字幕在講哪一顆。新增 `litKey()`／`clearLit()` 與 `.key.lit`（accent 藍＋外框），
  **一個步驟按過的鍵全部累積點亮**，下一步才清掉；純字幕步驟不亮任何鍵。與看打練習的 `.hint`（黃）分開，
  進 drill 時清掉。(2) **預設不自動播放**：`load()` 只自動演完**第一步**（空白畫面看起來像壞掉），
  之後每一步都要按。步驟演完就讓 `#btnNext` **閃爍**（`armNext()`／`disarmNext()`）；最後一步時
  title 改「下一課」、按下去載入下一課；整個系列的最後一步不閃（沒地方去了）。自動播放仍在 ▶。
  **閃爍用脈動外框不用換底色**——底色淡入淡出的中點會把字拉成灰的，按鈕在最該被按的時候看起來像 disabled。
  **實測抓到的 bug**：連按「下一步」比動畫快時，`pause()` 會 gen++ 讓進行中的 `stepTo` 中止，
  但 `si` 沒前進 → 卡在原地重播。修法＝新增 `pending`（正在演的步驟，跑在 `si` 前面），
  `pending > si` 時的點擊語意是**跳過動畫直接到那一步**，不是重來。(3) **側邊欄學習進度**：
  走到一課的最後一步就算完成，課號換成綠色 ✓，上方有「教學進度 N / 12」與進度條（全滿轉綠），
  附「重設」。存在 `localStorage['tutorialDone']`，**以課程 id 為鍵不是索引**，日後重排課程不會錯亂。
  **閃爍時機＝自動播放的時間（同日回饋）**：兩種模式跑**同一個時鐘**，差別只在時間到的時候做什麼——
  自動播放前進，手動則點亮「下一步」。`dwell(step)`（＝`min(8000, 1600 + 字幕長度 × 55)`）抽出來，
  `runStep()`（演鍵 → 顯示字幕 → 等閱讀時間）成為 `load`／`play`／`next`／`prev` 唯一的共同路徑，
  所以兩者不可能再各走各的。閱讀時間內按鈕不閃（那是給你看字幕的），時間到才開始閃。
  **左右手勘誤（2026-08-11 使用者回報）**：〈聲調〉課示範「上」用 `uh7`，字幕寫「ㄕㄤ 用左手拼完，
  右手接著補調」——**兩個都錯**。`IsLeftHand` 的左手是 `qwertasdfgzxcvb12345`，`u` 與 `h` **都在右手**，
  所以 `7`（也是右手）正好違反它當下要教的輪替原則；依 `ToneKeyFor`，最後一個字母鍵在右手時聲調鍵
  應留在左手的 `4`。已改成 `uh4` 並重寫字幕（「馬」`ma8` 原本就對：`a` 在左手 → 補調用右手）。
  guide.md §4.3 同一處也修掉，並補上左右手分界。**稽核抓不到這一類**：`uh7` 與 `uh4` 都是 3 鍵、
  畫面完全相同，Check B 只比鍵數；輪替只是速度上的建議而非硬規則（課文其他地方用 `wf2` 而不是
  `wf9` 是刻意的，教學用左手 1–5 比較好記），所以**不適合寫成稽核錯誤**——字幕與示範鍵互相矛盾
  終究只能靠人讀出來。
  以 Chrome 實測全部通過、無 console 錯誤。**測試環境的坑**：用 CDP 驅動時分頁是背景分頁，
  Chrome 會把 `setTimeout` 夾到最少 1000ms，量到的時間全部偏長、長腳本還會撞 CDP 45 秒逾時——
  那是量測假象不是程式問題，要嘛縮短 `dwell` 要嘛改用 `setInterval` 收集後再一次讀回。**注意**：改 `web\style.css` 後用 `python -m http.server`
  預覽時瀏覽器會沿用快取的舊樣式表（看起來像 CSS 沒生效），要硬重新整理或加 query 參數。

- 2026-08-10：**人類向使用手冊（`docs/guide.md`）＋教學課程重構＋教學稽核工具**。使用者回報教學網頁底部的
  「完整規格」連到 `docs/spec.md`，但那是工程交接記錄（掛日期、夾決策理由、被撤銷的功能、實作名詞），
  人類讀者看不懂；且教學課程「章節及內容有點混亂」、有過時（點名「的」還教成 `de`）與拿 bug 回報當教材
  （不鏽鋼、ㄋㄧㄠ）的問題。三件事：
  (a) **`docs/guide.md`**（繁中，12 節）——敘事主軸是**「怎麼把鍵數壓到最低」**而非功能清單：
  §1 講與微軟雙拼鍵位相同（zh→v/ch→i/sh→u/ing→;，肌肉記憶不用重練）、§3 完整鍵位表
  （含**過去完全沒文件化的 ㄐㄑㄒ ㄩ 系**與零聲母兩種寫法）、**§4 是全篇重點**（不打聲調就有字／
  單鍵音節 26 鍵全表／聲調鏡像為什麼存在／整句轉換／選字記憶／省鍵對照表）、§5 數字鍵兩種身分、
  §6 定案 vs 上屏、§11 疑難排解、§12 一頁速查表。**每一條示範鍵序都用 `repl.exe` 驗證過**——
  過程中發現 spec.md 沿用的三個例字在**單獨一個音節時是錯的**：`z4`→自不是字、`e4`→惡不是餓、
  `v`＋空白→之不是知（都是讀音對、但詞庫首選不同），手冊已改成實際會出現的字。
  (b) **教學課程 10 課 → 12 課、三階段**（入門／效率／情境），主軸是**依賴順序**：舊的〈單鍵音節〉
  排第 3 課卻整課靠空白鍵定案，而空白鍵第 5 課才教。新增〈韻母鍵：三呼與零聲母〉（原本完全沒教）
  與〈選字記憶〉（原本只是選字課的兩行字幕）；`intro`→`layout`（打字示範讓給 `basics`）、
  `space`→`settle`、`keys`→`homerow`。**四項勘誤**：標點課寫 `ni3hk`＋`,`→「你好，」而實際是
  **「你蒿，」**（標點用一聲／輕聲預設定案，字幕還特別標「ㄏㄠ→好」）、`d`+`l` 寫成 ㄉㄚ 實為 **ㄉㄞ**、
  「的」改教 `d`＋空白、ㄧ:ㄩ「4 倍」改為重量後的 1.6:1／2.3:1。
  (c) **`scripts\check-tutorials.ps1`**（使用者選定：**不做產生器，只做稽核工具**，tutorials.js 維持手寫）。
  兩種檢查：**畫面**（重播進真 Composer 比對已上屏／組字串／未定案注音／反白字／游標／候選清單與頁碼）
  與**最省鍵**（每個音節花的鍵數 vs `KeysForSyllable` 算出的最省）。後者是唯一抓得到「的＝`de`」這種
  **畫面完全正確、只是不再最省**的過時的檢查。**驗收方式＝先對舊課文跑**：報出全部 22 處
  （含你蒿、de、`ge`、以及我原本沒發現的「shift 課畫面寫 OK 但按鍵 `o``k` 實際產生 ok」），
  改完後對新課文全綠。逃生門：`alt: true`（刻意示範較長打法）、`audit: false`（引擎模擬不到的 TSF 層行為）。
  實作＝`check-tutorials.mjs`（Node，`eval` 載入 tutorials.js）＋ repl 新增 `--json`／`--shortest`；
  `KeysForSyllable`／`JsonString` 從 drill_gen 抽到 `cli\keystrokes.h`／`cli\json.h` 與 drill_gen 共用。
  **稽核腳本的坑**：偵測「一個音節花了幾鍵」不能只看 `unconfirmed` 變空——沒打聲調的音節是被
  **下一個音節的第一鍵**定案的，注音直接從 ㄢ 跳成 ㄑ 而不經過空字串，要改判「新的 unconfirmed
  不再是舊的前綴」才算換音節（否則 `ajqr2` 會被當成一個 4 鍵的 ㄑㄩㄢ 而誤報）。
  順修 `webpp.js` 兩處：nav 改成一階段一組、active 用 `data-lesson` 而非扁平位置索引；
  候選窗定位的 `compEl.children` 含 caret span，改用 `.ch` 才不會差一格。
  README 也補上手冊連結並修掉兩處與現行規格不符的舊描述（Shift「自動 commit」、聲調只寫 1–5）。
  **驗證**：check-tutorials 全綠、check-drill-coverage 仍 411 全覆蓋、`ctest` 173 全過、
  Chrome 實測 12 課 104 步全部渲染無誤、三個分組與看打練習都正常、無 console 錯誤。
  已 push；隨 v0.6 發佈（見 2026-08-12 條目）。

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
  已於同日打包（`art-shuangpin-v0.5-8-g0eebb7f.zip`）並以 `install.ps1` 重裝；隨 v0.6 發佈（見 2026-08-12 條目）。

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
