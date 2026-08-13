#!/bin/bash
# Double-clickable core check — see install.command for why a .command file
# begins with a cd and does not use `set -e`.
#
# This builds and runs build/repl, the repository's own CLI harness: it links
# the conversion core and the dictionary engine and NOTHING else — no AppKit,
# no InputMethodKit. It answers "is the engine fine on this Mac?" without the
# input method being installed or even selected, which is the first question
# worth answering when something is wrong.
#
# It used to run a local copy called artprobe, which existed only because
# cli/repl.cpp included <windows.h> and lived behind a read-only mirror.
# Both reasons are gone. Note the key dialect changed with it: repl takes
# `#` for the bare Shift tap, and it presses Enter itself when the sequence
# ends, so the old trailing `#` is neither needed nor correct.

cd "$(dirname "$0")" || exit 1

pause() {
    echo
    echo "────────────────────────────────────────────────────────"
    printf '按 Enter 關閉這個視窗。'
    read -r _
    exit "${1:-0}"
}

echo "阿特輸入法 — 只檢查輸入核心（不安裝、不碰系統）"
echo "資料夾：$(pwd)"
echo

if ! xcode-select -p >/dev/null 2>&1; then
    echo "找不到 Command Line Tools。請先在 Terminal 執行一次："
    echo
    echo "    xcode-select --install"
    pause 1
fi

# `make probe` runs preflight first, and preflight is what explains a missing
# ../out/data.txt and how to build it. No need to duplicate that here.
if ! make probe; then
    echo
    echo "核心編不起來。上面的訊息整段複製回報即可。"
    pause 1
fi

echo
echo "────────────────────────────────────────────────────────"
echo "打 ni3hk3vs（不用自己收尾，repl 在結束時會替你按一次 Enter）："
echo
./build/repl --data ../out/data.txt --keys "ni3hk3vs" || {
    echo
    echo "核心跑不起來——多半是詞庫 ../out/data.txt 有問題。"
    echo "重建一次就好（需要 python3，不必裝任何套件）："
    echo
    echo "    bash ../scripts/build-data.sh"
    pause 1
}

cat <<'EOF'

════════════════════════════════════════════════════════
最後一行如果出現   FINAL COMMIT: "你好中"
就表示輸入核心與詞庫在這台 Mac 上是好的。

（中間會看到「你好ㄓㄨㄥ」是正常的：最後一個音節在定案前
  維持注音顯示，按 Enter／空白才變成字。）

接下來雙擊 install.command 安裝。

如果組不出來，把整個視窗的內容複製回報。
════════════════════════════════════════════════════════
EOF

pause 0
