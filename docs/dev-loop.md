# 建置／註冊／迭代手冊

（M0-M3 進行中逐步完稿；目前為骨架＋已驗證事實。）

## 工具鏈

- VS2022 Build Tools：MSVC v143 + Windows 11 SDK（winget `Microsoft.VisualStudio.2022.BuildTools`，workload `VCTools` + recommended）
- CMake、Python 3（詞庫建置）、git

## 詞庫建置

```powershell
scripts\build-data.ps1   # data\ 來源 → out\data.txt（三步：frequency_builder → main_compiler → postprocess）
```

## IME 建置

- M0 階段：`ime\SampleIME.sln` retarget v143 後以 msbuild 建 x64 / Win32 Release。
- M1 起：頂層 CMake 建 engine / core / cli / tests（`ctest` 跑全部單元測試）。

## 教學網站與看打練習

```powershell
scripts\build-drills.ps1        # drills\lessons.txt → web\drills.js（兩趟：算覆蓋率 → 補完 → 產生）
python -m http.server -d web    # 本機預覽 http://localhost:8000
```

- `drills\lessons.txt` 是手寫課程，`drills\filler.txt` 由 `scripts\make-filler-lessons.py` 產生，**不要手改**。
- 產生器（`cli\drill_gen.cpp`）會用真的 `mspy::Composer` 重跑每一課，**打不出課文就直接失敗**——
  所以改了輸入行為之後一定要重跑一次，這是 core 之外最有效的迴歸測試。
- **練習不做任何選字**（2026-08-10）：整句轉換打錯的那一行，手寫課程算**硬錯誤**（要改寫句子，
  多半是「單獨一個字」輸給同音字，換成雙字詞就好），自動補完的課程則**整行丟掉**並把那個詞
  加進 `out\drill-banned.txt`，腳本重挑後再跑一次，直到每一行都乾淨為止（通常 2 輪）。
- 一聲與輕聲**不會出現在提示裡**：不打數字本來就是這兩個。
- **句號＝按兩次 Enter**（2026-08-10）：第一次上屏、第二次換行，課文因此一句一行。實作是
  `Runner::Press` 忠實模擬「composer 沒吃的鍵由應用程式收下」（`feedEnter` 在 kEmpty 回 `consumed=false`），
  換行字元也算課文的一個字（文章區畫成淡色 ↵）。逗號不換行，繼續同一段。
  **最後一句只按一次 Enter**（`TypeSentence` 的 `breakLine`）：後面沒有東西了，上屏就是結束。
- **按錯只出聲，畫面不動**（2026-08-10）：Web Audio 現場合成一聲柔和鐘聲（不放音檔，維持純靜態站台），
  音色照 Win 11 的 Default Beep（`C:\Windows\Media\Windows Background.wav`，微軟注音打錯時放的就是它）量測而來
  ——基音 F3 174.6 Hz ＋五度 C4 261.6 Hz ＋ F4／C5 泛音；原音要 1.3 秒才淡出，打字間隔放不下，尾巴砍到 0.45 秒。
  要調整改 `web\app.js` 的 `CHIME` 表（頻率／音量／延音三欄）即可；整組乘 2 就高八度。
  課文與鍵盤完全不標記錯誤。只有「可能是輸入」的鍵才響（字母、數字、標點、空白、Enter、Backspace）；
  Shift／Tab／方向鍵／功能鍵、按住不放的自動重複、以及打完整課之後都不響。
  右上角 🔊 可關，狀態記在 `localStorage` 的 `drillSound`。
- **打錯的字會留記號**（2026-08-10）：錯鍵記在 `drill.slips`（記到當下那個字的索引），該字打完顯示橘色
  而不是綠色，整課結束報「N 個字打錯過（共 M 字）」。**打的當下仍然不標記**，維持不干擾。
- **版面**：文章區固定四行整（`calc(1.75em*4+22px)`），鍵盤由 `fitKeyboard()` 依剩餘空間即時縮放
  ——練習列比教學列高，media query 看不到這件事，不縮就會蓋住上面的元件。
  **練習模式的記事本比教學模式矮**（`body.drilling`：260→152px、字 24→20px）：那塊只是照出輸入法在做什麼，
  真正要看的是文章和鍵盤；讓出來的空間剛好夠鍵盤畫回原尺寸（zoom 1.0）。
- 課文的詞若有多個讀音，產生器取詞庫分數最高的那個並印出 `note:`；不對就在詞後面加 `/讀音`。
- **課文不要出現結構助詞「得」**（跑得快、過得快）。教育部標準讀音是輕聲 `˙ㄉㄜ`（[國語小字典](https://dict.mini.moe.edu.tw/SearchIndex/word_detail?wordID=D0002532)），
  但詞庫裡 得(ㄉㄜ˙) 是地板分 −6.8、的(ㄉㄜ˙) 是 −1.62，整句轉換必定走「的」，而練習不做選字；
  若改標二聲 `ㄉㄜˊ` 就等於教錯讀音。改寫句子避開它。（實際輸入時：按 `8` 選一次「過得」，
  上下文記憶就會記住，同樣的前文以後自動出「得」——已用 repl 驗證。）
- **「什麼」在詞庫裡有四種鍵**（ㄕㄣˊ／ㄕㄜˊ × ㄇㄜ˙／ㄇㄛ˙）**分數完全相同**，產生器選哪個是看檔案順序，
  所以課文一律明寫 `什麼/ㄕㄣˊ-ㄇㄜ˙`（教育部三本辭典都只收 ㄕㄣˊ˙ㄇㄜ）。
- 每一行課文**必須以標點結尾**（標點才會把最後一個音節定案，產生器才驗得了）。
- **鍵位稽核**（2026-08-10）：`check-drill-coverage.ps1` 走遍鍵盤——26 個單鍵 ×（26+1）個第二鍵，
  用詞庫過濾後得到 **411 個拼得出來的音節**——再比對課文用到的，列出沒練到的（附鍵序、詞庫使用量、例字）。
  略過清單在 `drills\skip-syllables.txt`，**每一行都要寫理由**，不在清單上就是 exit 2。
  補完課程的目標集合改由 `drill_gen --reachable` 提供（不再用 99% 使用量門檻），
  且對沒有常用詞的音節放寬到 `FALLBACK_WORD_SCORE`，才有辦法練到 虐待／咱倆／喔唷 這種尾巴。
  現況：402 練得到、9 個明列略過。補完課程的用詞品質由兩道關卡把住：
  **`MIN_CHAR_SCORE = -6.3`**（詞裡任何一個字太生僻就整個詞不用——使用者點名的 耒耨欻裒煢衲 都在 −6.4 以下，
  而可接受的 虐咱倆唷嗲剖僧窮 都在 −6.2 以上），以及 `drillsvoid-words.txt` 這份手動排除清單。
  另外**允許多讀音的詞**了：課文會把讀音寫死（`剖析/ㄆㄡ-ㄒㄧ`），所以不會跟產生器的選擇打架——
  原本一律跳過多讀音詞，害得 ㄆㄡ 只剩「裒輯」可用。補完固定 **4 課**，字數平均分配。
- push `web/` 的變更會由 GitHub Actions 自動重佈 Pages。

## 註冊（一次性，管理員）

```powershell
scripts\register-dev.ps1        # icacls + regsvr32 x64 與 SysWOW64 x86
scripts\register-dev.ps1 -Unregister
```

事實（已由原始碼驗證）：
- 註冊寫入 HKCR/HKLM 並存 **DLL 路徑**——重建後**免重註冊**，覆蓋 DLL 即可。
- COM 註冊走 DLL 的 `DllRegisterServer`（`Server.cpp`）；TSF profile/類別註冊在 `Register.cpp`。
- 開發目錄需 `icacls … /grant "ALL APPLICATION PACKAGES:(OI)(CI)(RX)"`，否則 AppContainer 宿主（開始選單搜尋、Edge 欄位）載不進 DLL。

## 正式安裝 vs dev 註冊（**互斥，同一組 CLSID**）

兩種安裝法都把同一個 CLSID `{22DFB512-…}` 的 `InprocServer32` 指到某個 DLL **絕對路徑**，
後註冊的覆蓋先註冊的。系統同時只認得其中一份。

| | dev 註冊 | 正式安裝 |
|---|---|---|
| 腳本 | `scripts\register-dev.ps1` | `scripts\install.ps1`（或套件內 `install.bat`，會自我提權） |
| DLL 位置 | `out\deploy\{x64,x86}\SampleIME.dll` | `C:\Program Files\ArtShuangpin\{x64,x86}\ArtShuangpin.dll` |
| 更新方式 | `deploy-dev.ps1` 換檔即可，免重註冊 | `make-package.ps1` → 解壓 → 重跑 `install.ps1` |
| 適用 | TSF 實測迭代 | 日常使用 |

**dev 註冊把註冊表綁死在專案資料夾上**：搬動或刪除專案目錄，輸入法即刻失效
（載不到 DLL → 清單裡看得到名字但完全無反應，重開機後更明顯）。日常使用一律走正式安裝；
需要 TSF 實測時再跑 `register-dev.ps1` 把註冊搶回專案路徑，**測完記得重跑 `install.ps1` 裝回去**。
反之，正式安裝生效期間 `deploy-dev.ps1` 換掉 `out\deploy\` 的 DLL **不會反映到實際輸入法上**。

失效時的診斷指令（讀出目前系統認的路徑）：

```powershell
(Get-ItemProperty 'HKLM:\SOFTWARE\Classes\CLSID\{22DFB512-5772-4938-9FF1-EE24B3904B74}\InprocServer32').'(default)'
(Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Classes\CLSID\{22DFB512-5772-4938-9FF1-EE24B3904B74}\InprocServer32').'(default)'
```

註：舊註冊指向的 DLL 若已消失就無法正常反註冊（`regsvr32 /u` 需要 DLL 本身存在），
但 CLSID 相同，直接跑 `install.ps1` 會由 `DllRegisterServer` 覆寫路徑與 profile，等於就地修復。
使用者詞庫在 `%APPDATA%\MspyIME\`，不隨安裝位置移動。

## 迭代迴圈

1. 改碼 → `ctest` / `cli\repl` 驗證（日常九成在這層）。
2. 需要 TSF 實測時：重建 → `scripts\deploy-dev.ps1`（DLL 被鎖時自動 rename 舊檔再複製）→ 重開測試 app（記事本）。
3. 偶發：explorer/搜尋列持有舊 DLL → 登出再登入。

## 測試矩陣（M3/M4 逐步填實測結果）

| 宿主 | x64/x86 | 狀態 |
|---|---|---|
| Win11 記事本 | x64 | — |
| 傳統 Win32（如 notepad++） | x64 | — |
| 瀏覽器欄位（Edge/Chrome） | x64 | — |
| 32 位程式 | x86 | — |
| Word / Excel | x64 | — |
| 終端機 | x64 | — |
| LINE | x64 | — |
