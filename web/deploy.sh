#!/usr/bin/env bash
set -euo pipefail

HOST="playbach"
REMOTE_DIR="~/playbach"

echo "Syncing files to $HOST..."
rsync -az --delete \
  --exclude node_modules \
  --exclude .next \
  --exclude .git \
  "$(dirname "$0")/" "$HOST:$REMOTE_DIR/"

echo "Building and restarting containers..."
ssh "$HOST" "cd $REMOTE_DIR && docker compose up -d --build"

echo "Done. Site is live at https://playbach.io"
