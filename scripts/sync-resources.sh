#!/bin/bash
# Sync resource files into the PlayBach.app bundle
# Run from project root: ./scripts/sync-resources.sh

DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$DIR/build/release/PlayBach.app/Contents/Resources"

if [ ! -d "$DEST" ]; then
    echo "Bundle not found at $DEST — build first."
    exit 1
fi

cp -R "$DIR/resources/worlds" "$DIR/resources/Magnificat" "$DIR/resources/sounds" "$DEST/"
echo "Resources synced to bundle."
