#!/bin/bash
# Removes the input method. Self-contained on purpose: the release ZIP holds
# only the app and these two .command files, so this cannot delegate to
# mac/scripts/uninstall.sh the way the source-tree uninstaller does.
#
# See install.command for why a .command begins with a cd and avoids `set -e`.

cd "$(dirname "$0")" || exit 1

APP_NAME="ArtShuangpin"
DEST="$HOME/Library/Input Methods/$APP_NAME.app"
SUPPORT="$HOME/Library/Application Support/$APP_NAME"

pause() {
    echo
    echo "────────────────────────────────────────────────────────"
    printf '按 Enter 關閉這個視窗。'
    read -r _
    exit "${1:-0}"
}

echo "阿特輸入法 — 移除"
echo

if pgrep -x "$APP_NAME" >/dev/null 2>&1; then
    killall "$APP_NAME" >/dev/null 2>&1
    sleep 1
fi

if [ -d "$DEST" ]; then
    rm -rf "$DEST"
    echo "已移除：$DEST"
else
    echo "找不到已安裝的輸入法（可能已經移除過了）。"
fi

# The learned choices are the user's own data and outlive the app: a
# reinstall picks them back up. Ask rather than assume.
if [ -d "$SUPPORT" ]; then
    echo
    echo "還留著你的選字記憶："
    echo "  $SUPPORT"
    echo
    printf '要一起刪掉嗎？重新安裝的話它會被沿用。[y/N] '
    read -r answer
    case "$answer" in
        [Yy]*)
            rm -rf "$SUPPORT"
            echo "已刪除。"
            ;;
        *)
            echo "留著了。"
            ;;
    esac
fi

cat <<'EOF'

════════════════════════════════════════════════════════
輸入來源清單裡可能還留著一列，那只是系統的快取。

  系統設定 → 鍵盤 → 文字輸入 → 輸入來源 → 編輯…
  用「－」把「阿特輸入法」移掉即可。
════════════════════════════════════════════════════════
EOF

pause 0
