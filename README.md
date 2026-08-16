# 阿特雙拼輸入法（art-shuangpin）

Windows 與 macOS 的原生中文輸入法：**注音式輸入節奏＋微軟雙拼鍵位＋微軟注音式模態選字**，輸出繁體中文（zh-TW），全 C++、全 MIT。

兩個平台共用同一套輸入核心，打起來完全一樣：Windows 是 TSF（`ime/`），macOS 是 InputMethodKit（`mac/`）。

## 特色

- 每音節＝微軟雙拼兩鍵（zh→`v`、ch→`i`、sh→`u`、ing→`;`），聲調數字選打：不打＝一聲＋輕聲，`1`–`5` 精確，右手 `0`–`6` 為左右鏡像的等價鍵。
- **26 個字母鍵單獨按都是一個音節**（的＝`d`＋空白、了＝`l`＋空白），常用字兩鍵打完。
- 整句詞格轉換（McBopomofo 引擎），邊打邊修正；選字記憶綁上下文，改一次就生效。
- 組字中數字鍵全為控制鍵：`8` 開選單、`9`/`0` 移游標、`1`–`6` 選字、`7`/`8` 翻頁。
- Shift 單獨輕按＝中英切換（**不上屏**，英文長在同一個組字串裡，按語境補半形空白）。
- **上屏只認 `Enter`**（外加失焦與數字鍵盤）：標點、空白鍵、中英切換都只是把內容加進同一段未送出的組字串，整句寫多長都能回頭改。

**[使用手冊](docs/guide.md)**（鍵位、省鍵設計、完整按鍵表）；**[互動教學網站](https://weiwei84530.github.io/art-shuangpin/)**（3D 鍵盤動畫示範各項操作，附看打練習）；程式實作細節與決策記錄見 [docs/spec.md](docs/spec.md)。

## 安裝

每個 Release 都有兩份資產，各平台一份。

### Windows 10 / 11（x64）

1. 下載 `art-shuangpin-vX.Y.Z.zip` 並解壓（或自行建置後跑 `scripts\make-package.ps1` 打包）。
2. 以**系統管理員**開 PowerShell，執行解壓目錄中的 `install.ps1`。
3. 系統語言清單需已有「中文（台灣）」；輸入法會出現在該語言底下，名為「阿特雙拼輸入法 vX.Y.Z」。沒出現時先登出再登入。

移除：以系統管理員執行 `install.ps1 -Uninstall`。

### macOS 11 以上（Apple Silicon 與 Intel 通用）

1. 下載 `art-shuangpin-mac-vX.Y.Z.zip` 並解壓。
2. 對 `install.command` **按右鍵 →「打開」**（不能直接雙擊——那是所有非開發者憑證程式的一次性確認）。
3. 系統設定 → 鍵盤 → 文字輸入 → 輸入來源 → 編輯… → ＋，在「中文（繁體）」底下加入。

不需要 Xcode、git 或 Python，app 與詞庫都在 zip 裡。詳細說明與疑難排解見
[mac/docs/INSTALL.md](mac/docs/INSTALL.md)。

移除：執行同一個資料夾裡的 `uninstall.command`。

## 從原始碼建置

- **Windows**：VS2022 Build Tools（v143＋Win11 SDK）、CMake、Python 3。步驟見 [docs/dev-loop.md](docs/dev-loop.md)。
- **macOS**：只要 Command Line Tools（`xcode-select --install`），不需要完整的 Xcode。
  `bash scripts/build-data.sh` 建詞庫，再 `make -C mac install`。
  也可以直接雙擊 `mac/bootstrap.command`，它會把這些一次做完。

## 授權

MIT（見 [LICENSE](LICENSE)）。第三方來源——微軟 SampleIME 外殼、McBopomofo 引擎與詞庫——皆為 MIT/BSD，快照版本與修改記錄見 [docs/PROVENANCE.md](docs/PROVENANCE.md)。
