#!/bin/bash
# One-file bootstrap: clone this repo, rebuild vendor/ from public sources,
# build and install. The only thing that has to be carried to the Mac by hand.
#
# Double-click it, or:   bash bootstrap.command [target directory]
#
# It works from anywhere. Sitting next to a checkout (it ships inside the repo
# too) it updates that checkout in place; on its own in ~/Downloads it clones
# into ~/ArtShuangpin, or into $1 / $ARTMAC_DIR.
#
# Why this can exist at all: vendor/ is gitignored and never reaches a remote,
# but neither half of it is actually private -- see vendor.pin. The source
# mirror is a public repo and the 7.5 MB language model rides along in the
# public Windows release asset with the exact bytes we pin. So the only
# credentials needed anywhere are for THIS repo.
#
# See install.command for why a .command begins with a cd and avoids `set -e`.

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"

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

echo "阿特輸入法 — 一鍵取得並安裝"
echo

# --- where ------------------------------------------------------------------
#
# Running from inside a checkout means "update this one". The vendor.pin test
# is what distinguishes our own checkout from any other folder the file was
# dropped into.

if [ -f "$SELF_DIR/vendor.pin" ] && [ -d "$SELF_DIR/.git" ]; then
    REPO_DIR="$SELF_DIR"
    echo "在既有的 checkout 裡執行，直接更新它："
else
    REPO_DIR="${1:-${ARTMAC_DIR:-$HOME/ArtShuangpin}}"
    echo "安裝位置："
fi
echo "  $REPO_DIR"
echo

# --- Command Line Tools -----------------------------------------------------

if ! xcode-select -p >/dev/null 2>&1; then
    echo "找不到 Command Line Tools（git 與編譯器都在裡面）。"
    echo
    echo "請先在終端機執行一次，跳出系統視窗後按安裝："
    echo
    echo "    xcode-select --install"
    echo
    echo "裝完再回來雙擊這個檔案。"
    pause 1
fi

# --- the private repo -------------------------------------------------------
#
# The one step that needs credentials. Try gh first (its browser login is the
# least painful), fall back to plain git, which on macOS prompts once and then
# remembers the answer in the keychain.

# vendor.pin is not readable until the repo exists, so the clone URL is the
# one value that has to be duplicated here.
SHELL_REPO="https://github.com/weiwei84530/art-shuangpin-mac.git"
SHELL_REPO_SLUG="weiwei84530/art-shuangpin-mac"

if [ -d "$REPO_DIR/.git" ]; then
    echo "更新程式碼…"
    if ! git -C "$REPO_DIR" pull --ff-only; then
        die "git pull 失敗。如果你在那個資料夾裡改過東西，先處理掉再跑一次。"
    fi
else
    echo "取得程式碼（這個 repo 是私人的，可能會要求登入）…"
    mkdir -p "$(dirname "$REPO_DIR")" || die "無法建立 $(dirname "$REPO_DIR")"
    cloned=1
    if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
        gh repo clone "$SHELL_REPO_SLUG" "$REPO_DIR" && cloned=0
    fi
    if [ $cloned -ne 0 ]; then
        git clone "$SHELL_REPO" "$REPO_DIR" && cloned=0
    fi
    if [ $cloned -ne 0 ]; then
        cat <<EOF

取不到程式碼——這是私人 repo，需要先讓這台 Mac 有 GitHub 的存取權。
兩種做法，選一個做一次就好：

  A) 安裝 GitHub CLI 再登入（瀏覽器點一點，最省事）
         brew install gh
         gh auth login

  B) 不裝任何東西，用個人存取權杖（PAT）
         到 github.com → Settings → Developer settings
            → Personal access tokens → Tokens (classic) → Generate new token
         勾選 repo 權限，產生後把那串字複製起來。
         再跑一次這個檔案，git 問密碼時貼上權杖（不是 GitHub 密碼）。
         macOS 會把它記進鑰匙圈，之後不會再問。

弄好之後再雙擊這個檔案一次。
EOF
        pause 1
    fi
fi

cd "$REPO_DIR" || die "進不去 $REPO_DIR"
[ -f vendor.pin ] || die "$REPO_DIR/vendor.pin 不存在——這個資料夾看起來不是 art-shuangpin-mac。"

# shellcheck disable=SC1091
. ./vendor.pin

# --- vendor/art-shuangpin ---------------------------------------------------
#
# Public, shallow, pinned to a tag. A checkout sitting on the wrong tag is
# thrown away rather than fetched forward: it is 3 MB, and re-cloning has one
# failure mode instead of shallow-fetch's several.

echo
echo "取得輸入核心 ($ART_TAG)…"
current=""
if [ -d vendor/art-shuangpin/.git ]; then
    current="$(git -C vendor/art-shuangpin describe --tags --exact-match 2>/dev/null)"
fi
if [ "$current" != "$ART_TAG" ]; then
    rm -rf vendor/art-shuangpin
    mkdir -p vendor
    if ! git clone --depth 1 --branch "$ART_TAG" "$ART_REPO" vendor/art-shuangpin; then
        die "下載不到輸入核心。這個 repo 是公開的，通常是網路問題，晚點再試。"
    fi
else
    echo "  已經是 $ART_TAG，跳過"
fi
[ -f vendor/art-shuangpin/core/composer.cpp ] || die "輸入核心不完整（缺 core/composer.cpp）。"

# --- vendor/mspy-data.txt ---------------------------------------------------
#
# The hash is checked before downloading as well as after: on a re-run the
# file is normally already correct, and 4.5 MB is worth not re-fetching.

echo
echo "取得詞庫…"
have=""
if [ -f vendor/mspy-data.txt ]; then
    have="$(shasum -a 256 vendor/mspy-data.txt | awk '{print $1}')"
fi
if [ "$have" = "$MSPY_DATA_SHA256" ]; then
    echo "  已經有正確的詞庫，跳過"
else
    tmp="$(mktemp -d)" || die "建不出暫存資料夾"
    url="$ART_REPO_WEB/releases/download/$ART_TAG/$ART_ASSET"
    echo "  $url"
    if ! curl -fL --progress-bar -o "$tmp/asset.zip" "$url"; then
        rm -rf "$tmp"
        die "下載失敗。這是公開的釋出檔案，通常是網路問題，晚點再試。"
    fi
    if ! unzip -o -j -q "$tmp/asset.zip" "$ART_DATA_IN_ASSET" -d vendor/; then
        rm -rf "$tmp"
        die "解壓失敗：釋出檔裡找不到 $ART_DATA_IN_ASSET。vendor.pin 可能該更新了。"
    fi
    rm -rf "$tmp"
    got="$(shasum -a 256 vendor/mspy-data.txt | awk '{print $1}')"
    if [ "$got" != "$MSPY_DATA_SHA256" ]; then
        die "詞庫的 sha256 對不上：
  拿到 $got
  預期 $MSPY_DATA_SHA256
檔案已下載但不採信。請把這段訊息回報。"
    fi
    echo "  sha256 驗證通過"
fi

# --- hand over to the installer ---------------------------------------------
#
# exec rather than call: install.command does the CLT check, the signing
# identity, the clean build, the install and the closing instructions, and
# owns its own pause. Duplicating any of that here would mean two copies to
# keep in step.

echo
echo "程式碼與 vendor/ 都就位了，接下來交給 install.command。"
echo
exec bash "$REPO_DIR/install.command"
