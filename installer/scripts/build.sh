#!/usr/bin/env bash
# Build the installer app with the current Juice-Box release inside (fw/build.py --release first).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REL="$HERE/../build/fw/release"
[ -d "$REL" ] || { echo "no release: run .venv/bin/python fw/build.py --product BlackBox_NNN --release"; exit 1; }
find "$HERE/assets/release" -type f ! -name .keep -delete
cp "$REL"/*.json "$REL"/*-app.bin "$HERE/assets/release/"
export MACOSX_DEPLOYMENT_TARGET=11.0 CGO_CFLAGS="-mmacosx-version-min=11.0" CGO_LDFLAGS="-mmacosx-version-min=11.0"
cd "$HERE" && wails build -clean "$@"
