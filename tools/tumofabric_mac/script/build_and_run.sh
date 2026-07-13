#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT="TumoFabricMac"
APP="$ROOT/dist/$PRODUCT.app"
CONFIG="debug"
LAUNCH=1
VERIFY=0

for arg in "$@"; do
  case "$arg" in
    --release) CONFIG="release" ;;
    --no-launch) LAUNCH=0 ;;
    --verify) VERIFY=1 ;;
    *) print -u2 "Unknown option: $arg"; exit 2 ;;
  esac
done

export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

pkill -x "$PRODUCT" 2>/dev/null || true
swift build --package-path "$ROOT" -c "$CONFIG"
BIN_DIR="$(swift build --package-path "$ROOT" -c "$CONFIG" --show-bin-path)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/$PRODUCT" "$APP/Contents/MacOS/$PRODUCT"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>TumoFabricMac</string>
  <key>CFBundleIdentifier</key><string>com.squazaryu.tumofabric-mac</string>
  <key>CFBundleName</key><string>TumoFabric Mac</string>
  <key>CFBundleDisplayName</key><string>TumoFabric Mac</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

codesign --force --deep --sign - "$APP" >/dev/null

if (( LAUNCH )); then
  open -n "$APP"
fi

if (( VERIFY )); then
  sleep 2
  pgrep -x "$PRODUCT" >/dev/null
  print "$PRODUCT is running"
fi

print "$APP"
