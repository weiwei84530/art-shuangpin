# 上游來源與授權

本專案 vendor 的第三方原始碼來源與快照版本。**授權紅線：GPL-3 一律不用。**

| 目錄 | 上游 | Commit | 授權 |
|---|---|---|---|
| `ime\`（初始快照，之後為自有碼） | [microsoft/Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples) `Samples/IME/cpp` | `77f217b3f89d4dac7864a62cc91ff7b569f26a50` | MIT |
| `engine\gramambular2\` | [openvanilla/McBopomofo](https://github.com/openvanilla/McBopomofo) `Source/Engine/gramambular2` | `ee9941a6bdafe0fd58412207c04c1e985dd57b03` | MIT |
| `engine\mcbopomofo\` | 同上 `Source/Engine`（子集） | 同上 | MIT |
| `data\` | 同上 `Source/Data` | 同上 | MIT（`BPMFMappings.txt` 衍生自 libtabe `tsi.src`，BSD；見其 README.md） |

## Vendor 原則

- `ime\` 是**一次性快照**：SampleIME 之後被重度改寫為本專案的外殼（MspyIME），不追上游。
- `engine\` 原樣保留（含各 *Test.cpp），唯一修改：`MemoryMappedFile.cpp` 由 POSIX mmap 移植為 Win32 file mapping。修改處以 `// [MspyIME]` 註解標記。
- 排除未 vendor：`AssociatedPhrasesV2`、`VariantAnnotator`、`Mandarin/`（不需要——本專案的輸入核心直接產生注音字串）、各 *Benchmark.cpp。
- `data\` 原樣保留（含 curation Python 套件），由 `scripts\build-data.ps1` 建置 `out\data.txt`。
