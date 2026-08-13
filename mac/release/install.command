#!/bin/bash
# Installer for the prebuilt app in the release ZIP.
#
# Double-click it -- or right-click and choose 打開 the first time, which is
# what a downloaded, non-Developer-ID file needs.
#
# Not the same file as mac/install.command. That one builds from source and
# is for development; this one only copies an app that is already built, so
# it needs no Xcode Command Line Tools, no python3 and no git. The two share
# about thirty lines of idiom and nothing else, and this one is read by the
# person installing, so short is a feature.
#
# A .command file IS a shell script; Finder just knows to run it in Terminal
# rather than open it in an editor. Two consequences, both handled below:
# Finder starts it in $HOME rather than in the folder it lives in, so the
# first real statement has to be the cd; and the executable bit has to
# survive the download, which is why the release is a ZIP and not a bare
# file (a browser saves a bare .command 0644, and double-clicking it then
# says "you do not have appropriate access privileges").
#
# Deliberately NOT `set -e`: every failure path has to reach the pause at the
# end, or the Terminal window closes over the error message.
#
# User-facing output is 繁體中文; comments stay English.

cd "$(dirname "$0")" || exit 1

APP_NAME="ArtShuangpin"
APP="$APP_NAME.app"
DEST_DIR="$HOME/Library/Input Methods"
DEST="$DEST_DIR/$APP"
BUNDLE_ID="com.mspy.inputmethod.$APP_NAME"

pause() {
    echo
    echo "────────────────────────────────────────────────────────"
    printf '按 Enter 關閉這個視窗。'
    read -r _
    exit "${1:-0}"
}

echo "阿特輸入法 — 安裝"
echo "資料夾：$(pwd)"
echo

if [ ! -d "$APP" ]; then
    echo "找不到 $APP。"
    echo
    echo "這支程式要和 $APP 放在同一個資料夾裡執行。"
    echo "請把下載的 zip 整個解壓縮，再從解壓出來的資料夾裡打開它。"
    pause 1
fi

# --- quarantine --------------------------------------------------------------
#
# Anything downloaded by a browser carries com.apple.quarantine, and macOS
# refuses to load a quarantined app that is only ad-hoc signed -- it reports
# it as "damaged and can't be opened", which reads as a corrupt download
# rather than as a policy decision. Locally built apps never have the
# attribute, which is why mac/install.command has no equivalent of this.

echo "清除下載標記…"
xattr -dr com.apple.quarantine . 2>/dev/null
echo

# --- install -----------------------------------------------------------------
#
# The system keeps the input method running and holds the old binary open;
# it is relaunched on demand.

if pgrep -x "$APP_NAME" >/dev/null 2>&1; then
    echo "停掉正在執行的舊版本…"
    killall "$APP_NAME" >/dev/null 2>&1
    sleep 1
fi

mkdir -p "$DEST_DIR" || { echo "建不出 $DEST_DIR。"; pause 1; }
rm -rf "$DEST"
if ! cp -R "$APP" "$DEST"; then
    echo
    echo "複製失敗。上面的訊息整段複製回報即可。"
    pause 1
fi
echo "已安裝：$DEST"

# Registering the bundle makes the input source appear without a log out and
# back in. Not fatal if it fails -- it can still be added by hand.
if ! "$DEST/Contents/MacOS/$APP_NAME" --install; then
    echo "註冊沒成功，等一下用下面的步驟手動加入即可。"
fi

# --- Accessibility -----------------------------------------------------------
#
# Unconditional here, unlike the source installer's ad-hoc-only branch. TCC
# keys the Accessibility grant on the code directory hash, and every release
# is a different binary with a different hash, so a row left in System
# Settings from a previous version is already dead -- it stays visible, and
# ticked, while doing nothing. Clearing it costs nothing and saves the most
# confusing failure this project has.
#
# tccutil may or may not need root depending on the macOS version, so try as
# the user first and escalate only if that fails. A .command runs in an
# interactive Terminal, which is what lets sudo prompt here.

echo
echo "清掉舊版本留下的輔助使用授權…"
echo "（每一版的簽章都不同，那一列即使打著勾也已經失效）"
if tccutil reset Accessibility "$BUNDLE_ID" >/dev/null 2>&1; then
    echo "  已清除"
else
    echo
    echo "  這一步需要管理者密碼，請在下面輸入（不想做就按 Ctrl-C 跳過，"
    echo "  之後手動執行 tccutil reset Accessibility $BUNDLE_ID 也一樣）："
    if sudo tccutil reset Accessibility "$BUNDLE_ID"; then
        echo "  已清除"
    else
        echo "  跳過了，之後按 9 沒反應的話再手動清一次。"
    fi
fi
killall "$APP_NAME" >/dev/null 2>&1

cat <<'EOF'

════════════════════════════════════════════════════════
安裝完成。

【第一次安裝】要把輸入法加進系統：

  系統設定 → 鍵盤 → 文字輸入 → 輸入來源 → 編輯… → ＋
  在「中文（繁體）」底下選「阿特輸入法」加入。

【更新既有安裝】輸入來源不用重加，直接切過去打字就好。

  但系統會「快取」輸入來源的名稱與圖示。如果名稱或選單列圖示
  還是舊的樣子，到上面同一個頁面用「－」移除再用「＋」加回來；
  還是舊的就登出再登入一次。

【輔助使用權限】剛剛清掉了舊的授權，所以這次一定會跳出詢問。
  切到阿特輸入法，在任何地方按一次 9，跳出來時按「同意」。
  這樣 9 / 0 / - / = 才會變成游標移動鍵；不授權也能正常打字。

  每次更新版本都要重來一次這一步——輔助使用的授權綁在程式的
  簽章上，而每一版的簽章都不一樣。

要移除的話，執行同一個資料夾裡的 uninstall.command。
════════════════════════════════════════════════════════
EOF

pause 0
