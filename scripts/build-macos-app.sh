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

# Copy non-Qt homebrew dylib dependencies into Frameworks
echo "Bundling transitive dependencies..."
collect_deps() {
    for fw in "$FWDIR"/*.framework; do
        local fb="$fw/Versions/A/$(basename "$fw" .framework)"
        [ -f "$fb" ] && otool -L "$fb" 2>/dev/null
    done
    [ -f "$FWDIR/libverovio.dylib" ] && otool -L "$FWDIR/libverovio.dylib" 2>/dev/null
    otool -L "$EXECUTABLE" 2>/dev/null
}
# Recursively bundle all homebrew dylib dependencies (up to 5 levels deep)
for pass in 1 2 3 4 5; do
    new_libs=0
    # Collect all homebrew refs from all binaries in the bundle
    deps_file=$(mktemp)
    for binary in "$EXECUTABLE" "$FWDIR"/*.dylib "$FWDIR"/*.framework/Versions/A/*; do
        [ -f "$binary" ] || continue
        otool -L "$binary" 2>/dev/null | grep "/opt/homebrew" | awk '{print $1}' | sed 's|^@rpath/||'
    done | sort -u > "$deps_file"

    while read lib; do
        if [ -f "$lib" ]; then
            base=$(basename "$lib")
            if [ ! -f "$FWDIR/$base" ]; then
                cp -H "$lib" "$FWDIR/$base" 2>/dev/null
                chmod u+w "$FWDIR/$base" 2>/dev/null
                echo "  Bundled: $base"
                new_libs=$((new_libs + 1))
            fi
        fi
    done < "$deps_file"
    rm -f "$deps_file"

    echo "  Pass $pass: bundled $new_libs new libraries"
    [ "$new_libs" -eq 0 ] && break
done

# Rewrite all hardcoded /opt/homebrew paths to @rpath in the executable
rewrite_paths() {
    local binary="$1"
    otool -L "$binary" 2>/dev/null | grep "/opt/homebrew" | awk '{print $1}' | while read lib; do
        local clean=$(echo "$lib" | sed 's|^@rpath/||')
        if echo "$clean" | grep -q "\.framework/"; then
            # Framework: extract relative path (e.g. QtSvg.framework/Versions/A/QtSvg)
            local fw_name=$(echo "$clean" | sed 's|.*/\(Qt[^/]*\.framework/.*\)|\1|')
            install_name_tool -change "$lib" "@rpath/$fw_name" "$binary" 2>/dev/null
        else
            # Regular dylib: use just the filename
            local base=$(basename "$clean")
            install_name_tool -change "$lib" "@rpath/$base" "$binary" 2>/dev/null
        fi
    done
}

echo "Rewriting library paths in executable..."
rewrite_paths "$EXECUTABLE"

# Also rewrite libverovio reference if absolute
otool -L "$EXECUTABLE" 2>/dev/null | grep "libverovio" | grep -v "@rpath" | awk '{print $1}' | while read lib; do
    install_name_tool -change "$lib" "@rpath/libverovio.dylib" "$EXECUTABLE" 2>/dev/null
done

# Rewrite all bundled frameworks' internal deps too
echo "Rewriting library paths in frameworks..."
for fw in "$FWDIR"/*.framework; do
    fw_binary="$fw/Versions/A/$(basename "$fw" .framework)"
    [ -f "$fw_binary" ] && rewrite_paths "$fw_binary"
done
[ -f "$FWDIR/libverovio.dylib" ] && rewrite_paths "$FWDIR/libverovio.dylib"

# Also rewrite all bundled dylibs (transitive deps of transitive deps)
for dylib in "$FWDIR"/*.dylib; do
    [ -f "$dylib" ] && rewrite_paths "$dylib"
done

# Fix framework install names and add rpaths so they can find each other
for fw in "$FWDIR"/*.framework; do
    fw_name=$(basename "$fw" .framework)
    fw_binary="$fw/Versions/A/$fw_name"
    if [ -f "$fw_binary" ]; then
        # Fix install name from absolute to @rpath
        install_name_tool -id "@rpath/$fw_name.framework/Versions/A/$fw_name" "$fw_binary" 2>/dev/null || true
        install_name_tool -add_rpath @loader_path/../../ "$fw_binary" 2>/dev/null || true
    fi
done

# Fix dylib install names too
for dylib in "$FWDIR"/*.dylib; do
    if [ -f "$dylib" ]; then
        base=$(basename "$dylib")
        install_name_tool -id "@rpath/$base" "$dylib" 2>/dev/null || true
    fi
done

# Clean up rpaths on the executable
set +e
install_name_tool -delete_rpath /opt/homebrew/lib "$EXECUTABLE" 2>/dev/null
install_name_tool -delete_rpath /opt/homebrew/opt/qt/lib "$EXECUTABLE" 2>/dev/null
install_name_tool -delete_rpath "$BUILD_DIR/build/verovio" "$EXECUTABLE" 2>/dev/null
install_name_tool -delete_rpath "$BUILD_DIR/lib" "$EXECUTABLE" 2>/dev/null
install_name_tool -add_rpath @executable_path/../Frameworks "$EXECUTABLE" 2>/dev/null
set -e

# Make all framework files writable (Qt installs them read-only,
# which prevents xattr -cr from working without sudo on other machines)
chmod -R u+w "$FWDIR" "$PLUGDIR"

# Ad-hoc sign the bundle (fixes "damaged" error on macOS Sequoia+)
echo "Signing..."
codesign --deep --force --sign - "$APP" 2>/dev/null

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
