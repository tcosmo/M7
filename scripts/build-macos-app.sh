#!/bin/bash
set -e

# Build a standalone PlayBach.app for macOS (Apple Silicon)
# Usage: ./scripts/build-macos-app.sh [Release|Debug]

BUILD_TYPE="${1:-Release}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/release"
QT_LIB="/opt/homebrew/lib"
QT_PLUGINS="/opt/homebrew/share/qt/plugins"
APP="$BUILD_DIR/PlayBach.app"

echo "=== PlayBach macOS Bundle Builder ==="
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"
echo "Type:    $BUILD_TYPE"
echo ""

# Step 1: Configure
echo "[1/5] Configuring CMake..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" 2>&1 | tail -3

# Step 2: Build
echo "[2/5] Building..."
cmake --build "$BUILD_DIR" --target playbach 2>&1 | tail -3

# Step 3: Copy Qt frameworks
echo "[3/5] Copying Qt frameworks..."
FWDIR="$APP/Contents/Frameworks"
# Remove old frameworks (they may have read-only permissions from Qt)
rm -rf "$FWDIR"
mkdir -p "$FWDIR"

QT_FRAMEWORKS=(
    QtCore QtGui QtWidgets QtNetwork
    QtWebEngineCore QtWebEngineWidgets QtWebChannel
    QtQuick QtQuickWidgets QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript
    QtOpenGL QtPrintSupport QtPositioning QtDBus QtSvg QtPdf QtXml
)

for fw in "${QT_FRAMEWORKS[@]}"; do
    src="$QT_LIB/${fw}.framework"
    if [ -d "$src" ]; then
        cp -RH "$src" "$FWDIR/"
        # Remove headers to save space
        rm -rf "$FWDIR/${fw}.framework/Headers"
        rm -rf "$FWDIR/${fw}.framework/Versions/A/Headers"
    fi
done

# Fix WebEngineProcess helper permissions and copy
chmod -R u+w "$FWDIR/QtWebEngineCore.framework/" 2>/dev/null || true
HELPERS="$QT_LIB/QtWebEngineCore.framework/Versions/A/Helpers"
if [ -d "$HELPERS" ]; then
    cp -RH "$HELPERS" "$FWDIR/QtWebEngineCore.framework/Versions/A/"
fi

# Step 4: Copy Qt plugins
echo "[4/5] Copying Qt plugins..."
PLUGDIR="$APP/Contents/PlugIns"
rm -rf "$PLUGDIR"
mkdir -p "$PLUGDIR"
for plug in platforms imageformats tls; do
    if [ -d "$QT_PLUGINS/$plug" ]; then
        cp -RH "$QT_PLUGINS/$plug" "$PLUGDIR/"
    fi
done

# Write qt.conf so the app finds plugins in the bundle
cat > "$APP/Contents/Resources/qt.conf" << 'EOF'
[Paths]
Plugins = PlugIns
EOF

# Step 5: Fix rpaths
echo "[5/5] Fixing rpaths..."
EXECUTABLE="$APP/Contents/MacOS/PlayBach"

# Copy libverovio into Frameworks
cp "$BUILD_DIR/build/verovio/libverovio.dylib" "$FWDIR/" 2>/dev/null || \
cp "$BUILD_DIR/libverovio.dylib" "$FWDIR/" 2>/dev/null || true

# Remove non-bundle rpaths, keep @loader_path/../Frameworks and @executable_path/../Frameworks
set +e
install_name_tool -delete_rpath /opt/homebrew/lib "$EXECUTABLE" 2>/dev/null
install_name_tool -delete_rpath "$BUILD_DIR/build/verovio" "$EXECUTABLE" 2>/dev/null
install_name_tool -delete_rpath "$BUILD_DIR/lib" "$EXECUTABLE" 2>/dev/null
# Ensure the Frameworks rpath exists
install_name_tool -add_rpath @executable_path/../Frameworks "$EXECUTABLE" 2>/dev/null
set -e

# Verify only the Frameworks rpath remains
echo ""
echo "=== rpaths ==="
otool -l "$EXECUTABLE" | grep -A2 "LC_RPATH" | grep "path" || echo "(none)"

echo ""
echo "=== Bundle contents ==="
echo "Frameworks: $(ls "$FWDIR" | wc -l | tr -d ' ')"
echo "Plugins:    $(find "$PLUGDIR" -name '*.dylib' | wc -l | tr -d ' ')"
echo "Resources:  $(ls "$APP/Contents/Resources/" | tr '\n' ' ')"
echo "Size:       $(du -sh "$APP" | cut -f1)"
echo ""
echo "=== PlayBach.app ready at ==="
echo "$APP"
echo ""
echo "To create a DMG for sharing:"
echo "  hdiutil create -srcfolder '$APP' -volname PlayBach PlayBach.dmg"
