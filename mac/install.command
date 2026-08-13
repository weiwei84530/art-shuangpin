#!/bin/bash
# Double-clickable installer for people who do not want to open Terminal.
#
# A .command file IS a shell script; Finder just knows to run it in Terminal
# instead of opening it in an editor. Two things follow, and both are handled
# below because getting either wrong is a confusing failure rather than a
# clear one:
#
#   * Finder starts it in $HOME, not in the folder it lives in, so the first
#     real statement has to be the cd.
#   * The executable bit has to survive the trip from the Windows host.
#     tools/make_transfer_zip.py stores 0755 for .command entries; if that is
#     ever lost, a double-click opens this file in TextEdit and the fix is
#     `chmod +x` (START-HERE.txt says so).
#
# Deliberately NOT `set -e`: every failure path has to reach the pause at the
# end, or the Terminal window closes on the error message before it is read.
#
# User-facing output is 繁體中文 like docs/INSTALL.md — this script exists to
# be read by the person installing, not by whoever is editing the repo.

cd "$(dirname "$0")" || exit 1

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

# --- Command Line Tools ------------------------------------------------------

if ! xcode-select -p >/dev/null 2>&1; then
    echo "找不到 Command Line Tools，沒有編譯器就沒辦法建置。"
    echo
    echo "請先在 Terminal 執行一次（會跳出系統的安裝視窗）："
    echo
    echo "    xcode-select --install"
    echo
    echo "裝完之後再回來雙擊這個檔案。"
    pause 1
fi

# --- the language model ------------------------------------------------------
#
# out/ is gitignored, so the 7.5 MB model is a build product no clone carries.
# The Makefile's preflight target says this too, but by then it has already
# scrolled past a screen of build output; catching it here keeps the message
# as the first thing on screen -- and unlike the old missing-vendor case, the
# fix is one command with nothing to install, so just run it.

if [ ! -f ../core/composer.cpp ]; then
    echo "找不到 ../core/composer.cpp。"
    echo
    echo "這支程式要在 art-shuangpin 的 mac/ 目錄裡執行，"
    echo "不能單獨搬到別的地方。"
    pause 1
fi

if [ ! -f ../out/data.txt ]; then
    echo "還沒有詞庫，先建一份。"
    echo "（需要 python3，不必裝任何套件，大約一兩分鐘。）"
    echo
    if ! bash ../scripts/build-data.sh; then
        echo
        echo "詞庫建不起來。上面的訊息整段複製回報即可。"
        pause 1
    fi
    echo
fi

# --- signing identity --------------------------------------------------------
#
# Ad-hoc signatures change on every build and TCC keys the Accessibility grant
# on exactly that, so idle 9/0/-/= quietly stop working after each install. A
# self-signed certificate fixes it permanently. Use it automatically if the
# user has already created one (docs/NOTES.md has the one-time steps); never
# nag about it here, because everything except those four keys works fine
# without it.

SIGN_ID="-"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "ArtShuangpin Dev"; then
    SIGN_ID="ArtShuangpin Dev"
    echo "找到自簽憑證「ArtShuangpin Dev」，用它簽章"
    echo "（輔助使用權限就不會因為重建而失效）。"
    echo
fi

# --- build -------------------------------------------------------------------
#
# Always a clean build. This script is the path for someone who does not want
# to think about the build at all, and a stale object file linking an old
# input core back in looks exactly like "the update did not work" — a minute
# of rebuild is the cheaper failure.

if [ -d build ]; then
    echo "清掉上次的建置產物…"
    make clean >/dev/null || true
fi

echo "建置中，大約需要一分鐘…"
echo
if ! make SIGN_ID="$SIGN_ID"; then
    echo
    echo "建置失敗。上面最後幾行紅字就是原因，整段複製回報即可。"
    pause 1
fi

echo
echo "安裝到 ~/Library/Input Methods …"
echo
if ! make install SIGN_ID="$SIGN_ID"; then
    echo
    echo "安裝失敗。上面的訊息整段複製回報即可。"
    pause 1
fi

# --- Accessibility -----------------------------------------------------------
#
# ONLY in the ad-hoc case, and the condition is the whole point. TCC keys the
# Accessibility grant on the code directory hash, which an ad-hoc signature
# changes on every build -- so the row left in System Settings is already dead
# and clearing it costs nothing. With the self-signed certificate the grant
# SURVIVES the rebuild, and resetting would gratuitously revoke a working
# permission and demand re-approval for no reason.
#
# tccutil may or may not need root for Accessibility depending on the macOS
# version, so try as the user first and escalate only if that fails. A
# .command runs in an interactive Terminal, which is what lets sudo prompt for
# the password here instead of failing silently.

BUNDLE_ID="$(defaults read "$PWD/dist/ArtShuangpin.app/Contents/Info" \
             CFBundleIdentifier 2>/dev/null || echo com.mspy.inputmethod.ArtShuangpin)"

if [ "$SIGN_ID" = "-" ]; then
    echo
    echo "清掉上一次建置留下的輔助使用授權…"
    echo "（臨時簽章每次建置都不同，那一列即使打著勾也已經失效）"
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
    # The input method has to restart before it re-checks its trust state.
    killall ArtShuangpin >/dev/null 2>&1 || true
fi

# --- what to do next ---------------------------------------------------------

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

EOF

# The closing paragraph depends on which signature was used, so it is not part
# of the block above: with the certificate there is nothing to re-approve.
if [ "$SIGN_ID" = "-" ]; then
    cat <<'EOF'
【輔助使用權限】舊的授權剛剛已經清掉了，所以這次一定會跳出詢問。
  切到阿特輸入法，在任何地方按一次 9，跳出來時按「同意」。
  這樣 9 / 0 / - / = 才會變成游標移動鍵；不授權也能正常打字。

  不想每次重裝都重來一遍，就做一張自簽憑證，做一次就好：
  「鑰匙圈存取」→ 憑證輔助程式 → 建立憑證…
  名稱填 ArtShuangpin Dev、識別類型選「自我簽署的根憑證」、
  憑證類型選「程式碼簽署」。之後這支安裝程式會自己找到它，
  權限就不會再因為重建而失效，上面那段清除也不會再執行。
EOF
else
    cat <<'EOF'
【輔助使用權限】這次用自簽憑證簽章，所以之前的授權還有效，
  不需要重新同意。按 9 / 0 / - / = 應該直接就能移動游標。
EOF
fi

echo "════════════════════════════════════════════════════════"

pause 0
