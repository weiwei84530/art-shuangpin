#!/bin/bash
# Double-clickable uninstaller — see install.command for why a .command file
# begins with a cd and does not use `set -e`.
#
# Asks before touching the learned vocabulary, because that is the one thing
# here the user cannot get back: everything else is rebuilt from this folder
# in a minute.

cd "$(dirname "$0")" || exit 1

pause() {
    echo
    echo "────────────────────────────────────────────────────────"
    printf '按 Enter 關閉這個視窗。'
    read -r _
    exit "${1:-0}"
}

SUPPORT="$HOME/Library/Application Support/ArtShuangpin"
CHOICES="$SUPPORT/user-choices.txt"
# Parked by the first v0.4.0 launch: it predates the contextual store and
# cannot be converted, so it is kept rather than read.
LEGACY="$SUPPORT/user-phrases.txt.bak"

echo "阿特輸入法 — 移除"
echo

echo "要一併刪掉學過的使用者詞嗎？"
if [ -f "$CHOICES" ]; then
    echo "  （$CHOICES）"
else
    echo "  （目前還沒有學過的詞）"
fi
if [ -f "$LEGACY" ]; then
    echo "  （另有舊版留下的 $LEGACY，也會一起刪除）"
fi
printf '輸入 y 連同使用者詞一起刪除，直接按 Enter 則保留：'
read -r answer
echo

if [ "$answer" = "y" ] || [ "$answer" = "Y" ]; then
    bash ./scripts/uninstall.sh --purge
else
    bash ./scripts/uninstall.sh
fi
status=$?

if [ $status -ne 0 ]; then
    echo
    echo "移除過程有錯誤，上面的訊息整段複製回報即可。"
    pause 1
fi

cat <<'EOF'

════════════════════════════════════════════════════════
已從 ~/Library/Input Methods 移除。

輸入來源清單裡那一項可能還在，到
  系統設定 → 鍵盤 → 文字輸入 → 輸入來源 → 編輯…
用「－」把它移掉。
════════════════════════════════════════════════════════
EOF

pause 0
