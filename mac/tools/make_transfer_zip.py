"""Pack everything the Mac needs into one ZIP.

The Mac has no access to this repository: `vendor/` is gitignored and the
language model is a 7.5 MB build product, so a `git clone` on the other side
would not be buildable.  This script produces a single self-contained archive
to carry across.

    python tools/make_transfer_zip.py              full archive
    python tools/make_transfer_zip.py --no-vendor  source only (~120 KB)

Use --no-vendor for the second and later rounds: vendor/ does not change
between fixes, so re-sending 8 MB of dictionary every time is waste.  Unzip
it over the previous extraction.

Three things this script exists to get right, all of which the obvious tools
get wrong:

  * ZIP entry names use forward slashes.  PowerShell 5.1's Compress-Archive
    is known to write backslashes, which macOS then extracts as files with
    literal backslashes in their names.
  * The shell scripts keep their executable bit.  Writing the mode into
    `external_attr` is only HALF of it, and the missing half is silent:
    `zipfile.ZipInfo` sets `create_system = 0` (MS-DOS) when it is
    constructed on Windows, and an extractor reading a create_system-0 entry
    treats the low byte as DOS attributes and IGNORES the Unix mode in the
    high 16 bits.  The archive then looks correct to `zipfile` on this host
    and extracts 0644 on the Mac.  `create_system = 3` (Unix) is what makes
    macOS read the mode at all.  Symptom when this is wrong: double-clicking
    install.command gives "could not be executed because you do not have
    appropriate access privileges", and Get Info says "You can read and
    write" with no execute.  verify() below re-opens the finished archive and
    checks both halves, because this failure is invisible until the far side.
  * No CR survives into a shell script.  A single one on the shebang line
    makes bash fail with "bad interpreter: /bin/bash^M", which reads as the
    file being corrupt rather than as a line-ending problem.  .gitattributes
    already pins these to LF; this is the check that says so out loud if
    something writes one anyway.

Standard library only.  Never writes anything outside this repo.
"""

import argparse
import hashlib
import sys
import time
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOT_IN_ZIP = "ArtMac"

# Everything in the repo proper that the Mac build touches.  The .command
# files sit at the top level on purpose: they are what the user double-clicks,
# so they have to be visible the moment the folder is opened in Finder.
REPO_ITEMS = [
    "Makefile",
    "VERSION",
    "README.md",
    "CLAUDE.md",
    "vendor.pin",
    "bootstrap.command",
    "check-engine.command",
    "install.command",
    "uninstall.command",
    "docs",
    "resources",
    "scripts",
    "src",
    "tools",
]

# Suffixes that must be executable on the other side, and must not contain a
# single CR anywhere.
EXECUTABLE_SUFFIXES = {".sh", ".command"}

# vendor/art-shuangpin is carried without these:
#   .git   a 4 MB clone of history that no build step reads;
#   data/  9 MB of dictionary *sources* for the Python pipeline that
#          produces mspy-data.txt -- and the product itself is already in
#          the archive, prebuilt.
VENDOR_SKIP_DIRS = {".git", "data"}

SKIP_NAMES = {".DS_Store", "__pycache__", ".pytest_cache"}
SKIP_SUFFIXES = {".pyc", ".pyo"}

START_HERE = """阿特輸入法 macOS 版 — 從這裡開始

需要 macOS 11 以上，以及 Command Line Tools（不需要完整 Xcode）。
沒裝過的話，在「終端機」執行一次這行，跳出系統視窗後按安裝：

    xcode-select --install


═══ 只想複製一個檔案就搞定的話 ═══

    bootstrap.command      自己 git clone、自己抓 vendor/、然後建置安裝

把這一支單獨複製到 Mac 上任何地方雙擊，它會把整個專案拉到
~/ArtShuangpin 並一路裝完。之後要更新，就雙擊 ~/ArtShuangpin/
裡面的同一支檔案，會 git pull 再重裝——不用再傳任何壓縮檔。

第一次會要求 GitHub 登入（程式碼在私人 repo）；輸入核心與詞庫
都在公開的地方，不需要任何權限。詳情看 docs/INSTALL.md。


═══ 這個資料夾裡可以雙擊的檔案 ═══

    install.command        建置並安裝（更新也是按這個）
    check-engine.command   只檢查輸入核心，不安裝、不碰系統
    uninstall.command      移除
    bootstrap.command      更新程式碼與 vendor/ 之後再做上面那件事

雙擊會自己開「終端機」跑完，你不用打任何指令。
每一支跑完都會停住等你按 Enter，看得完訊息再關。

    ※ 如果雙擊之後是用「文字編輯」打開，而不是跑起來，代表檔案的
      執行權限在傳輸過程掉了。在終端機執行一次就好（把資料夾拖進
      終端機視窗可以直接得到路徑）：

          chmod +x /把/這裡/換成/這個資料夾/*.command

    ※ 如果跳出「無法打開，因為它來自未識別的開發者」，在檔案上按
      右鍵 →「打開」→ 再按一次「打開」。這個確認每支檔案只會問一次。


═══ 第一次安裝 ═══

    1)  雙擊 check-engine.command

        只編譯輸入核心與詞庫引擎，完全不碰 macOS 介面框架。
        看到組出「你好中」就表示引擎在這台機器上是好的。
        （這步可以跳過，但第一次做一遍，之後出事比較好判斷。）

    2)  雙擊 install.command

        會自動建置、安裝到 ~/Library/Input Methods/ 並註冊輸入來源。
        大約一分鐘。

    3)  系統設定 → 鍵盤 → 文字輸入 → 輸入來源 → 編輯… → ＋
        在「中文（繁體）」底下加入「阿特輸入法」。


═══ 更新既有的安裝 ═══

    1)  把這一包解壓「覆蓋」到上次那個資料夾。

        更新包（不含 vendor/）一定要覆蓋上去，解到全新的空資料夾
        會因為缺少輸入核心與詞庫而編不起來。

    2)  雙擊 install.command

        它會先清掉上次的建置產物再重建（含 vendor/ 的更新包一定要
        這樣，否則上次留下的 .o 檔會把舊版核心直接連回去，看起來
        像是沒更新），然後關掉正在跑的輸入法、換掉它、重新註冊。
        輸入來源不必重加。

    3)  切到阿特輸入法，打幾個字確認新版有生效。

        這一版換了選單列圖示的形狀與輸入來源的名稱。系統會快取這兩樣，
        沒更新的話到 系統設定 → 鍵盤 → 輸入來源，用「－」移除再用「＋」
        加回來；還是舊的就登出再登入一次。


═══ 還是想自己打指令的話 ═══

    make clean && make install       等同 install.command
    make probe                       等同 check-engine.command
    bash scripts/uninstall.sh        等同 uninstall.command


═══ 更新後 9 / 0 / - / = 沒反應時 ═══

這幾乎一定是輔助使用權限失效，不是輸入法壞掉。先確認症狀：

    按一次 9 —— 如果又跳出授權詢問，就是這個情況。
    （真的授權好的時候不會再問。）

原因：本機的臨時簽章每次建置都不一樣，而系統是認簽章的。
系統設定 → 隱私權與安全性 → 輔助使用 裡那一列還在、
也還打著勾，但它對應的是「上一次建置」的版本，對新版無效。

清掉重來：

    tccutil reset Accessibility com.mspy.inputmethod.ArtShuangpin
    killall ArtShuangpin

然後按一次 9，在跳出來的詢問按「同意」。

不想每次重建都弄一次，就做一個自簽憑證，權限不會再因重建失效：

    「鑰匙圈存取」→ 憑證輔助程式 → 建立憑證…
    名稱填 ArtShuangpin Dev
    識別類型選「自我簽署的根憑證」
    憑證類型選「程式碼簽署」

只要做這一次。之後 install.command 會自己找到它並拿來簽章，
你不用改任何指令。詳細步驟見 docs/NOTES.md 的
〈Ad-hoc signing and Accessibility〉。


═══ 出問題時 ═══

docs/INSTALL.md（中文，含疑難排解）
docs/NOTES.md（英文，含「第一個該查的地方」清單）

任何一步的錯誤訊息整段複製回去貼給我就好。
"""


def should_skip(path: Path) -> bool:
    if path.name in SKIP_NAMES or path.suffix in SKIP_SUFFIXES:
        return True
    return any(part in SKIP_NAMES for part in path.parts)


def collect(root: Path, prefix: str, skip_dirs=frozenset()):
    """Yields (absolute path, name inside the zip), sorted, files only."""
    if root.is_file():
        yield root, prefix
        return
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if relative.parts and relative.parts[0] in skip_dirs:
            continue
        if should_skip(relative):
            continue
        yield path, f"{prefix}/{relative.as_posix()}"


def add(archive: zipfile.ZipFile, source: Path, name: str) -> int:
    data = source.read_bytes()
    executable = source.suffix in EXECUTABLE_SUFFIXES
    if executable and b"\r" in data:
        sys.exit(
            f"{source} contains a CR.\n"
            f"bash on macOS would fail with \"bad interpreter: /bin/bash^M\".\n"
            f"Rewrite it with LF endings; .gitattributes pins these to LF, so\n"
            f"something bypassed git to write this file."
        )
    info = zipfile.ZipInfo(name, date_time=time.localtime(source.stat().st_mtime)[:6])
    info.compress_type = zipfile.ZIP_DEFLATED
    # Unix, not MS-DOS. ZipInfo defaults this to 0 on Windows, and with 0 the
    # mode below is never even looked at by the extractor. See the module
    # docstring -- this single assignment is the whole difference between a
    # .command that runs when double-clicked and one that reports "you do not
    # have appropriate access privileges".
    info.create_system = 3
    # 0o100755 / 0o100644: regular file, executable for shell scripts and for
    # the double-clickable .command wrappers.
    mode = 0o755 if executable else 0o644
    info.external_attr = (0o100000 | mode) << 16
    archive.writestr(info, data)
    return len(data)


def verify(output: Path) -> None:
    """Re-opens the finished archive and checks what the Mac will actually see.

    Everything here is cheap and every one of these has a failure mode that is
    invisible on this host: a wrong create_system, a lost mode, a backslash in
    an entry name, a CR in a script. Checking the artifact rather than the
    intent is the only way to know.
    """
    problems = []
    with zipfile.ZipFile(output) as archive:
        entries = archive.infolist()
        if not any(i.filename.endswith(".command") for i in entries):
            problems.append("no .command files in the archive at all")
        for info in entries:
            if "\\" in info.filename:
                problems.append(f"{info.filename}: backslash in the entry name")
            if info.create_system != 3:
                problems.append(
                    f"{info.filename}: create_system={info.create_system}, "
                    f"so the Unix mode will be ignored on extraction"
                )
            expected = 0o755 if Path(info.filename).suffix in EXECUTABLE_SUFFIXES else 0o644
            mode = (info.external_attr >> 16) & 0o777
            if mode != expected:
                problems.append(
                    f"{info.filename}: mode {oct(mode)}, expected {oct(expected)}"
                )
    if problems:
        sys.exit(
            "the archive is wrong; NOT sending it:\n  "
            + "\n  ".join(problems[:20])
        )


def make_bootstrap_zip(output: str | None) -> int:
    """The release asset: bootstrap.command alone, at the zip root.

    A .command served over HTTP arrives with no mode at all -- browsers save
    it 0644 -- so downloading the bare file from a release page reproduces
    exactly the "you do not have appropriate access privileges" dialog that
    the create_system fix above was written for. A ZIP is the only container
    that carries the bit, which is why the release ships this as well as the
    plain file.
    """
    version = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    path = Path(output) if output else (
        REPO / "dist" / f"ArtShuangpin-bootstrap-{version}.zip"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    source = REPO / "bootstrap.command"
    if not source.exists():
        sys.exit(f"missing: {source}")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        add(archive, source, "bootstrap.command")
    verify(path)
    print(f"{path}")
    print(f"  bootstrap.command, {path.stat().st_size:,} bytes")
    print("  verified: Unix entry at 0755")
    return 0


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--no-vendor",
        action="store_true",
        help="omit vendor/ (source-only archive for follow-up rounds)",
    )
    parser.add_argument(
        "--bootstrap-only",
        action="store_true",
        help="just bootstrap.command, at the zip root (the release asset)",
    )
    parser.add_argument("-o", "--output", help="output path (default dist/...)")
    args = parser.parse_args()

    if args.bootstrap_only:
        return make_bootstrap_zip(args.output)

    version = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    stamp = time.strftime("%Y%m%d-%H%M")
    kind = "src" if args.no_vendor else "full"
    default = REPO / "dist" / f"ArtShuangpin-mac-{version}-{kind}-{stamp}.zip"
    output = Path(args.output) if args.output else default
    output.parent.mkdir(parents=True, exist_ok=True)

    entries = []
    for item in REPO_ITEMS:
        path = REPO / item
        if not path.exists():
            sys.exit(f"missing: {path}")
        entries.extend(collect(path, f"{ROOT_IN_ZIP}/{item}"))

    if not args.no_vendor:
        data = REPO / "vendor" / "mspy-data.txt"
        mirror = REPO / "vendor" / "art-shuangpin"
        if not data.exists() or not (mirror / "core" / "composer.cpp").exists():
            sys.exit(
                "vendor/ is not populated -- run `python tools/sync_art.py` first"
            )
        entries.append((data, f"{ROOT_IN_ZIP}/vendor/mspy-data.txt"))
        entries.extend(
            collect(mirror, f"{ROOT_IN_ZIP}/vendor/art-shuangpin", VENDOR_SKIP_DIRS)
        )

    total = 0
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        info = zipfile.ZipInfo(f"{ROOT_IN_ZIP}/START-HERE.txt", time.localtime()[:6])
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3  # Unix; see add()
        info.external_attr = (0o100000 | 0o644) << 16
        # Which kind of archive this is decides the very first instruction, so
        # state it up front instead of leaving the reader to infer it from
        # whether vendor/ happens to be in the listing.
        banner = (
            "※ 這是「更新包」，不含 vendor/（輸入核心與詞庫）。\n"
            "  請解壓覆蓋到上次那個資料夾，不要解到新的空資料夾。\n\n"
            if args.no_vendor
            else "※ 這是「完整包」，含 vendor/，可以解壓到任何位置。\n\n"
        )
        archive.writestr(info, (banner + START_HERE).encode("utf-8"))
        for source, name in entries:
            total += add(archive, source, name)

    verify(output)

    packed = output.stat().st_size
    print(f"{output}")
    print(f"  {len(entries) + 1} files, {total / 1e6:.1f} MB -> {packed / 1e6:.1f} MB")
    print("  verified: Unix entries, .command/.sh at 0755, no backslashes, no CR")
    if not args.no_vendor:
        print(f"  mspy-data.txt sha256 {sha256(REPO / 'vendor' / 'mspy-data.txt')}")
    print()
    print("Unzip on the Mac, then follow ArtMac/START-HERE.txt.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
