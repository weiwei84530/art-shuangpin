# M3 設計筆記 — 挖除範例引擎、接上 mspy_core

## 原則

**保留 `CCompositionProcessorEngine` 類名與被外部引用的公開介面**（SampleIME.cpp 啟動路徑、CandidateListUIPresenter 等都在用），只掏空實作改為委派給 `mspy::Composer`。KeyEventSink → KeyStateCategory → KeyHandler 的分派結構不動，改寫各 `_Handle*` 的內容。

## 按鍵流程（現況，已讀碼確認）

1. `KeyEventSink.cpp:_IsKeyEaten`（:61）：查 compartment 開關 → `ConvertVKey` → **`IsVirtualKeyNeed(code, wch, _IsComposing(), _candidateMode, …, pKeyState)` 決定吃不吃 + {Category, Function}**。OnTestKeyDown/OnKeyDown 都走這裡——**必須是無副作用的判斷**。
2. `_InvokeKeyHandler` → `CKeyHandlerEditSession::DoEditSession` → `CKeyStateCategoryFactory` 依 Category 取 handler → 依 Function 呼叫 `CSampleIME::_Handle*`。

## 新流程設計

### Composer 需新增（core\）
- `bool wouldConsume(char c) const` — 無副作用版的 feedChar 消耗判斷，給 IsVirtualKeyNeed 用：
  - Selecting → true（可列印鍵全吃）
  - 數字 1-5：pending complete ∨ (pending empty ∧ lastWasBare_) → true；否則 Composing→true、Empty→false
  - `,` `.` → 恆 true；a-z → 恆 true；`;`：Composing→true、Empty→false
  - 空白/6-9/0/其他可列印：Composing→true（commit+字面）、Empty→false

### IsVirtualKeyNeed 對映（引擎 adapter 內重寫）
- 可列印字元：`wouldConsume` → CATEGORY_COMPOSING / FUNCTION_INPUT（`,`/`.` 也走 INPUT，composer 內部處理）
- VK_BACK → COMPOSING/FUNCTION_BACKSPACE（Composing/Selecting 時吃）
- VK_RETURN → COMPOSING/FUNCTION_FINALIZE_TEXTSTORE；VK_ESCAPE → FUNCTION_CANCEL
- VK_DOWN（Composing）→ **FUNCTION_CONVERT**（語意＝開候選窗）；VK_UP（Selecting）→ CANDIDATE/FUNCTION_CANCEL
- Selecting 時數字 1-9 → CATEGORY_CANDIDATE/FUNCTION_SELECT_BY_NUMBER；PageUp/Down → MOVE_PAGE_*
- VK_LEFT/RIGHT：v0.1 Composing 時吃掉不動作（避免游標逃出組字區）
- 其餘（含 Empty 狀態的空白/數字/`;`）→ 不吃（回 FALSE，App 自行處理）

### KeyHandler 改寫核心：`_SyncComposerState(ec, pContext, result)`
每個 handler = 餵 composer + 呼叫 sync：
1. `result.commitText` 非空 → 組字範圍設為 commitText → `_TerminateComposition`（沿用 _HandleCompositionFinalize 的做法）→ 關候選 UI。
2. composer Empty → 清組字文字 → terminate → 關候選 UI。
3. 否則 → `_AddComposingAndChar(composer.composedText())`；Selecting → 確保 presenter 存在並 `_SetText(候選轉 CCandidateListItem)`；非 Selecting → `_EndCandidateList`。
- 需 UTF-8 → UTF-16 助手（composedText/candidates 是 std::string UTF-8）。

### 候選選字回路
CATEGORY_CANDIDATE 的 SELECT_BY_NUMBER / FINALIZE_CANDIDATELIST：從 presenter 取選中索引（頁面座標→全域索引）→ `composer.selectCandidate(idx)` → sync（關窗回 Composing）。

### 引擎 adapter（CompositionProcessorEngine.cpp 掏空重寫）
- 持有 `shared_ptr<McBopomofoLM>`（DLL 同層 `mspy-data.txt`，deploy 腳本從 out\data.txt 複製改名）+ `RelaxedToneLM` + `Composer`。
- 保留供外部呼叫的：GetCandidateListIndexRange、GetCandidateWindowWidth、IsPunctuation（改：只認 `,`/`.`? v0.1 沿用範例表亦可）、GetLocale、Setup* 系列（改為載入小麥詞庫）。
- 刪除/停用：TableDictionaryEngine、DictionarySearch、File/FileMapping（引擎不再用，但檔案可先留著編譯，之後清）。

### 建置整合（vcxproj）
- Include dirs：`..\..\core`、`..\..\engine`、`..\..\engine\mcbopomofo`
- Link：`..\..\build\Release\mspy_core.lib`、`..\..\build\Release\mcb_engine.lib`（x64；Win32 需另建 32 位版 CMake build dir `build32`）
- `/utf-8` 編譯旗標（我們的標頭含 UTF-8 中文註解與注音字串）
- CRT：CMake 預設 /MD，vcxproj Release 預設 MultiThreadedDLL──需確認一致
- C++20（vcxproj 設 LanguageStandard stdcpp20）

### 身分更名（M3 最後一步，能動之後才做）
新 GUID 全套（Globals.cpp）、名稱「Wei雙音拚輸入法」、TEXTSERVICE_DIC 改 mspy-data.txt、反註冊舊的再註冊新的。

## 驗收（docs/spec.md §8 對照）
記事本：`vs`→中、`vs3`→種、`wo3`→我、`de`→的/`de1`→得、`ni3hk3`→你好、↓ 開窗數字選字、`,`→「，」、空白/Esc/Backspace 各狀態、2026 直接出數字。
