#!/bin/bash
# Removes ArtShuangpin from ~/Library/Input Methods.
#
#   bash scripts/uninstall.sh            keep learned user phrases
#   bash scripts/uninstall.sh --purge    delete them too
set -euo pipefail

APP_NAME="ArtShuangpin"
DEST="$HOME/Library/Input Methods/$APP_NAME.app"
SUPPORT="$HOME/Library/Application Support/ArtShuangpin"

if pgrep -x "$APP_NAME" >/dev/null 2>&1; then
  killall "$APP_NAME" >/dev/null 2>&1 || true
  sleep 1
fi

if [ -d "$DEST" ]; then
  rm -rf "$DEST"
  echo "removed: $DEST"
else
  echo "not installed: $DEST"
fi

if [ "${1:-}" = "--purge" ]; then
  if [ -d "$SUPPORT" ]; then
    rm -rf "$SUPPORT"
    echo "removed: $SUPPORT  (learned user phrases)"
  fi
else
  if [ -d "$SUPPORT" ]; then
    echo "kept: $SUPPORT  (learned user phrases; --purge removes it)"
  fi
fi

cat <<'EOF'

The entry may linger in System Settings > Keyboard > Text Input >
Input Sources until you remove it there with the "-" button.
EOF
