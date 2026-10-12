#!/bin/bash
# build_app.sh -- builds "GoW2 Recomp.app" (native SwiftUI launcher) into the
# repo root. The app drives gow2_launcher.py (validation, mods, autosave) and
# the local g2play build; it contains no game code or assets.
#
#   launcher/macos/build_app.sh            -> ./GoW2 Recomp.app
#   APP=/Applications/GoW2\ Recomp.app launcher/macos/build_app.sh
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SRC/../.." && pwd)"
APP="${APP:-$REPO/GoW2 Recomp.app}"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

swiftc -O -parse-as-library -swift-version 5 \
    -target arm64-apple-macos14.0 \
    "$SRC"/App.swift "$SRC"/Backend.swift "$SRC"/Settings.swift "$SRC"/OverlaySettingsFile.swift "$SRC"/Views.swift \
    "$SRC"/FileAnalysis.swift "$SRC"/PatchYAML.swift "$SRC"/PatchStore.swift "$SRC"/Theme.swift "$SRC"/Decor.swift \
    -o "$APP/Contents/MacOS/GoW2Recomp"

# Banner: a screenshot of the native renderer already in docs/img.
cp "$REPO/docs/img/palace-lighting.jpg" "$APP/Contents/Resources/hero.jpg"
# Icon: the local game app's icon when present (not versioned -- game artwork).
ICON_SRC="$REPO/God of War II HD.app/Contents/Resources/AppIcon.icns"
ICON_KEY=""
if [ -f "$ICON_SRC" ]; then
    cp "$ICON_SRC" "$APP/Contents/Resources/AppIcon.icns"
    ICON_KEY="<key>CFBundleIconFile</key><string>AppIcon</string>"
fi

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>GoW2 Recomp</string>
  <key>CFBundleDisplayName</key><string>GoW2 Recomp</string>
  <key>CFBundleIdentifier</key><string>dev.andrebrum.gow2recomp.launcher</string>
  <key>CFBundleExecutable</key><string>GoW2Recomp</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>GoW2RepoPath</key><string>$REPO</string>
  $ICON_KEY
</dict></plist>
EOF

codesign --force --sign - "$APP" >/dev/null 2>&1 || true
echo "built: $APP"
