# 阿特雙拼輸入法（art-shuangpin）

Windows 原生 TSF 中文輸入法：**注音式輸入節奏＋微軟雙拼鍵位＋微軟注音式模態選字**，輸出繁體中文（zh-TW），全 C++、全 MIT。

## 特色

- 每音節＝微軟雙拼兩鍵（zh→`v`、ch→`i`、sh→`u`、ing→`;`），聲調數字選打：不打＝一聲＋輕聲，`1`–`5` 精確。
- 整句詞格轉換（McBopomofo 引擎），邊打邊修正，附使用者選字學習。
- 組字中數字鍵全為控制鍵：`8` 開選單、`9`/`0` 移游標、`1`–`6` 選字、`7`/`8` 翻頁。
- Shift 單獨輕按＝中英切換（自動 commit＋按語境補空白）。

完整輸入方案規格見 [docs/spec.md](docs/spec.md)；**[互動教學網站](https://weiwei84530.github.io/art-shuangpin/)**（3D 鍵盤動畫示範各項操作）。

## 安裝（Windows 10 / 11，x64）

1. 下載 Release 的 `art-shuangpin-*.zip` 並解壓（或自行建置後跑 `scripts\make-package.ps1` 打包）。
2. 以**系統管理員**開 PowerShell，執行解壓目錄中的 `install.ps1`。
3. 系統語言清單需已有「中文（台灣）」；輸入法會出現在該語言底下，名為「阿特雙拼輸入法」。沒出現時先登出再登入。

移除：以系統管理員執行 `install.ps1 -Uninstall`。

## 從原始碼建置

需 VS2022 Build Tools（v143＋Win11 SDK）、CMake、Python 3。步驟見 [docs/dev-loop.md](docs/dev-loop.md)。

## 授權

MIT（見 [LICENSE](LICENSE)）。第三方來源——微軟 SampleIME 外殼、McBopomofo 引擎與詞庫——皆為 MIT/BSD，快照版本與修改記錄見 [docs/PROVENANCE.md](docs/PROVENANCE.md)。
