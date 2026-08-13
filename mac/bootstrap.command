#!/bin/bash
# One-file bootstrap for DEVELOPMENT: clone or update the repository, build
# the dictionary, build and install. The only thing that has to be carried to
# the Mac by hand.
#
# Double-click it, or:   bash bootstrap.command [target directory]
#
# It works from anywhere. Sitting inside a checkout (it ships in the repo
# too) it updates that checkout in place; on its own in ~/Downloads it clones
# into ~/art-shuangpin, or into $1 / $ARTMAC_DIR.
#
# NOT the normal way to install. Most people should download the release zip
# and double-click the install.command inside it: that carries a prebuilt
# universal app and needs no git, no Command Line Tools and no python3. This
# path exists to run code that has not been released yet, which is why it
# compiles from source.
#
# It needs no credentials at all. The repository is public, and everything
# the build wants is in it -- the language model is built here from data/
# rather than downloaded, so there is no pinned tag and no release asset to
# keep in step. Before the two halves were merged this file had to clone a
# private repo, and the failure that produced was the reason for the merge.
#
# See install.command for why a .command begins with a cd and avoids `set -e`.

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_URL="https://github.com/weiwei84530/art-shuangpin.git"

pause() {
    echo
    echo "────────────────────────────────────────────────────────"
    printf '按 Enter 關閉這個視窗。'
    read -r _
    exit "${1:-0}"
}

die() {
    echo
    echo "$1"
    pause 1
}

echo "阿特輸入法 — 取得原始碼並安裝（開發用）"
echo

# --- where ------------------------------------------------------------------
#
# Running from inside a checkout means "update this one". mac/Makefile is
# what distinguishes our own checkout from any other folder the file was
# dropped into.

if [ -f "$SELF_DIR/Makefile" ] && [ -d "$SELF_DIR/../.git" ]; then
    REPO_DIR="$(cd "$SELF_DIR/.." && pwd)"
    echo "在既有的 checkout 裡執行，直接更新它："
else
    REPO_DIR="${1:-${ARTMAC_DIR:-$HOME/art-shuangpin}}"
    echo "安裝位置："
fi
echo "  $REPO_DIR"
echo

# --- Command Line Tools -----------------------------------------------------

if ! xcode-select -p >/dev/null 2>&1; then
    echo "找不到 Command Line Tools（git、編譯器與 python3 都在裡面）。"
    echo
    echo "請先在終端機執行一次，跳出系統視窗後按安裝："
    echo
    echo "    xcode-select --install"
    echo
    echo "裝完再回來雙擊這個檔案。"
    pause 1
fi

# --- the repository ---------------------------------------------------------
#
# Public, so this never asks for a password. A full clone rather than a
# shallow one: the whole history is under 3 MB, and `git pull --ff-only` on a
# shallow clone has failure modes that a full one simply does not have.

if [ -d "$REPO_DIR/.git" ]; then
    echo "更新程式碼…"
    if ! git -C "$REPO_DIR" pull --ff-only; then
        die "git pull 失敗。如果你在那個資料夾裡改過東西，先處理掉再跑一次。"
    fi
else
    echo "取得程式碼…"
    mkdir -p "$(dirname "$REPO_DIR")" || die "無法建立 $(dirname "$REPO_DIR")"
    if ! git clone "$REPO_URL" "$REPO_DIR"; then
        die "下載不到程式碼。這個 repo 是公開的，通常是網路問題，晚點再試。"
    fi
fi

cd "$REPO_DIR/mac" || die "進不去 $REPO_DIR/mac——這個資料夾看起來不是 art-shuangpin。"

# --- the language model -----------------------------------------------------
#
# A build product: out/ is gitignored, so a clone does not carry it. Built
# from the tracked sources in data/ with python3 and nothing else. Skipped
# when it is already there -- data/ changes rarely and this takes minutes.

if [ -f ../out/data.txt ]; then
    echo
    echo "詞庫已經有了，跳過。"
else
    echo
    echo "建立詞庫（需要 python3，不必裝套件，大約一兩分鐘）…"
    echo
    if ! bash ../scripts/build-data.sh; then
        die "詞庫建不起來。上面最後幾行就是原因，整段複製回報即可。"
    fi
fi

# --- hand over to the installer ---------------------------------------------
#
# exec rather than call: install.command does the signing identity, the clean
# build, the install and the closing instructions, and owns its own pause.
# Duplicating any of that here would mean two copies to keep in step.

echo
echo "程式碼與詞庫都就位了，接下來交給 install.command。"
echo
exec bash "$REPO_DIR/mac/install.command"
