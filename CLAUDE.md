# Wei雙音拚輸入法 — 專案說明

## 專案目標

Windows 11 原生 TSF 輸入法：**注音式輸入節奏 + 微軟雙拼鍵位 + 微軟注音式的模態選字**。
取代先前在 Rime／小狼毫上的 `mspy_zhuyin` 方案（該方案受限於 Rime 架構：數字鍵無法選字、候選窗無法按需顯示）。

完整計劃與決策記錄：`C:\Users\weiwe\.claude\plans\windows-11-swift-meadow.md`（歷史參考）；本檔為現行狀態的權威來源。

## 架構（全 C++、全 MIT）

```
ime\    SampleIME 衍生 TSF 外殼（微軟官方範例 → 自有碼；候選窗、COM、註冊）
core\   輸入核心（自寫）：雙拼解析、聲調規則、Composing/Selecting 模態狀態機
engine\ 小麥注音引擎子集（gramambular2 詞格轉換 + McBopomofoLM + UserOverrideModel）
data\   小麥詞庫來源（Python 建置 → out\data.txt）
cli\    REPL 測試臺（日常開發主力，不碰 TSF）
```

- 引擎藏在 `core\` 的介面後（`relaxed_tone_lm` 配接器），日後可抽換。
- **授權紅線：GPL-3 一律不用**。現行全棧 MIT/BSD，來源與 sha 見 `docs/PROVENANCE.md`。

## 輸入方案規格（摘要，完整版見 docs/spec.md）

- 每音節＝微軟雙拼 2 鍵（zh→v、ch→i、sh→u、ing→`;`）＋選擇性聲調數字。
- **聲調語意（嚴格，已拍板勿反覆）**：不打數字＝只出一聲+輕聲；明打 `1`＝只出一聲；`5`＝只輕聲；2/3/4 精確。
- 模態選字：組字中行內整句轉換、無候選窗；↓ 開窗後數字 1-9 選字；Esc/↑ 關窗。
- 空白鍵＝輸出空白字元；`,`/`.` 直接上屏「，」「。」。
- 顯示名稱「Wei雙音拚輸入法」，zh-TW（LANGID 0x0404），輸出繁體。

## Git 約定（覆寫全域規則）

- 純本地 repo，**無 remote、不 push**。
- **自動 commit**：使用者授權 agent 全權保管版本，做完一組有意義的修改就 commit，不需徵求同意。
- 直接在當前分支操作，不開分支/worktree。
- Commit message 英文、一行摘要＋必要時補充；**訊息要具體**（使用者靠它回溯，例如 "Port MemoryMappedFile to Win32 file mapping" 而非 "update engine"）。

## 建置與開發迴圈（詳見 docs/dev-loop.md）

- 工具鏈：VS2022 Build Tools（v143 + Win11 SDK）、CMake、Python 3。
- 詞庫：`scripts\build-data.ps1` → `out\data.txt`。
- 註冊：`scripts\register-dev.ps1`（管理員）；之後重建只需 `scripts\deploy-dev.ps1` 換 DLL + 重開測試 app，**免重註冊**。
- 日常開發：`ctest` + `cli\repl` 為主，TSF 實測為輔。

## 工具使用注意

- **讀取含中文的檔案一律用 Read 工具，不要用 `Get-Content`**（PowerShell 主控台編碼會把 UTF-8 中文變亂碼）。必要時先 `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8`。
- 詞庫資料鍵的輕聲 `˙` 是**尾綴**（`ㄉㄜ˙`），顯示慣例是前綴（˙ㄉㄜ）——查詢層與顯示層嚴格分離，勿混用。
- 程式註解一律英文；docs 與本檔繁體中文。

## 狀態記錄

- 2026-07-26：專案啟動。計劃核准（架構歷經三輪收斂：fork 新酷音 → 自建本體+chewing 函式庫 → 因 GPL-3 紅線改為 SampleIME 外殼 + 小麥引擎全 MIT 棧）。
- 2026-07-26：**M1 完成**——引擎+詞庫函式庫化驗證通過（CLI：ㄒㄧㄣㄎㄨˋㄧㄣ…→新酷音輸入法；data.txt 需 LF，build-data.ps1 已處理）。**M2 完成**——core\ 四模組 + 124 tests 全綠；REPL `--keys` 實測：`ni3hk3`→你好（含整句自動修正 你蒿→你好）、`de`→的、`wo`→窩/`wo3`→我、`{`開窗數字選字、`,`→「，」。設計要點：**音節第二鍵落下即以無聲調 eager 進詞格（見字即所得），聲調數字對剛進格音節回填**。
- 2026-07-26：**M0d 完成**——SampleIME retarget v143（WPO off、C4463 bitfield 修正）雙架構建置；LANGID 已提前改 0x0404（掛在中文(台灣)下，免裝簡中）。已部署至 out\deploy 並以管理員註冊（HKCR/CTF TIP 已驗證）。
- 2026-07-26：**M0e 完成（使用者實測通過）**。兩個 Win11 坑已修：(1) `ITfContextView::GetWnd` 回 NULL → 候選窗無主不顯示，`GetFocus()` 後備（CandidateListUIPresenter.cpp）；(2) `TEXTSERVICE_DIC` 是**DLL 同層**的檔名（無子資料夾），部署時勿放 Dictionary\。除錯基礎設施：`MSPY_DEBUG_LOG`（Private.h 開關）寫 `%TEMP%\MspyIME.debug.log`。
- 2026-07-27：**M3 主體接線完成**，設計見 docs/m3-design.md。要點：MspyBridge（ime\）持有 LM+Composer；`IsVirtualKeyNeedMspy` 全面接管按鍵路由（原樣本邏輯 #if 0 保留）；KeyHandler 各 `_Handle*` 改為「餵 composer + `_SyncComposer`」單一模式；Esc/↑ 在選字時走 CANDIDATE/FINALIZE_CANDIDATELIST（只關窗）；數字選字以「頁內編號→字串→比對 composer 候選」解析；詞庫部署為 DLL 同層 `mspy-data.txt`。vcxproj：/utf-8 **/permissive**（樣本舊碼過不了 C++20 嚴格模式）、C++20、連 build\ 與 build32\ 的 mspy_core+mcb_engine。使用者實測迭代修正：候選窗（GetWnd 後備＋詞典路徑）、`_AddComposingAndChar` 以選取位置裁切組字範圍造成游標中移時右側文字重複（改 `_SetCompositionText` 直設整段）、**OnCompositionTerminated 改為保留文字（點他處＝自動上屏）並重置 composer**、標點全表（雾凇式，引號開閉交替）、雙色顯示（黑＝聲調已定、藍＝未定；**全段虛線底線直到 commit**）、←/→ 組字內移動游標、空白＝commit（與 Enter 同）。
- 2026-07-27：**M4 完成，tag v0.1**。UOM 學習（容量 500、半衰期 90 分；手動選字 observe、每次插入後 suggest 回套）；多音節手選詞持久化 `%APPDATA%\MspyIME\user-phrases.txt`（附加＋去重＋重載；單字僅 UOM 工作階段學習，避免 score-0 永久壓排序）；**正名「Wei雙音拚輸入法」，13 組 GUID 全部重生成（新 CLSID 22DFB512-5772-4938-9FF1-EE24B3904B74），舊 CLSID 已反註冊**。127 tests 全綠。
