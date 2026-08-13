#!/bin/bash
# Installs dist/ArtShuangpin.app into ~/Library/Input Methods.
#
# Run it through `make install`, or directly:  bash scripts/install.sh
set -euo pipefail

APP_NAME="ArtShuangpin"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/dist/$APP_NAME.app"
DEST_DIR="$HOME/Library/Input Methods"
DEST="$DEST_DIR/$APP_NAME.app"

if [ ! -d "$SRC" ]; then
  echo "not built yet: $SRC"
  echo "run 'make' first."
  exit 1
fi

# The system keeps the input method running; it holds the old binary open and
# would go on serving the old code. It is relaunched on demand.
if pgrep -x "$APP_NAME" >/dev/null 2>&1; then
  echo "stopping the running input method"
  killall "$APP_NAME" >/dev/null 2>&1 || true
  sleep 1
fi

mkdir -p "$DEST_DIR"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"
echo "installed: $DEST"

# Registering the bundle means the input source shows up without a log out
# and back in. Failure here is not fatal — it can still be added by hand.
if ! "$DEST/Contents/MacOS/$APP_NAME" --install; then
  echo "note: could not self-register; add it by hand (see below)"
fi

BUNDLE_ID="$(defaults read "$DEST/Contents/Info" CFBundleIdentifier 2>/dev/null || echo com.mspy.inputmethod.ArtShuangpin)"

cat <<EOF

Next:
  System Settings > Keyboard > Text Input > Input Sources > Edit… > +
  and pick 阿特雙拼輸入法 under Chinese (Traditional).

If it is not in that list, the input method did not register — see
docs/NOTES.md, section "The input source does not appear".

Idle 9/0/-/= navigation additionally needs this app ticked under
System Settings > Privacy & Security > Accessibility. The input method asks
the first time you press one of those keys.

IMPORTANT after a rebuild: an ad-hoc signature is identified by its code
directory hash, which changes every time the binary does. An entry already
sitting in that Accessibility list belongs to the PREVIOUS build — it stays
visible, and ticked, while doing nothing. If 9/0/-/= are dead, clear it:

    tccutil reset Accessibility $BUNDLE_ID

then press 9 once and grant the prompt. To stop this recurring, sign with a
self-signed certificate instead of ad-hoc — the grant then survives rebuilds:

    make install SIGN_ID="ArtShuangpin Dev"

docs/NOTES.md, "Ad-hoc signing and Accessibility", has the one-time setup.
EOF
