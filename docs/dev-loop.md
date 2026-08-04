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
