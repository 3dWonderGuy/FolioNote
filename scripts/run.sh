#!/bin/bash
set -e

# Resolve repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Build binary path
BIN_PATH="$REPO_ROOT/build/bin/FolioNote"

# Ensure binary exists
if [ ! -f "$BIN_PATH" ]; then
    echo "[FolioNote Launcher] Binary not found at $BIN_PATH. Building now..."
    ninja -C "$REPO_ROOT/build"
fi

# If running inside a flatpak container (e.g. Flatpak VS Code), spawn on host display
if [ -f "/.flatpak-info" ] && command -v flatpak-spawn >/dev/null 2>&1; then
    exec flatpak-spawn --host /bin/bash -c "cd '$REPO_ROOT' && DISPLAY=\${DISPLAY:-:0} '$BIN_PATH' \"\$@\"" -- "$@"
else
    # Standard desktop execution
    exec "$BIN_PATH" "$@"
fi

