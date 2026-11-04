#!/usr/bin/env bash
# Prepare a Juice-Box release: build the firmware release, put it in the installer, commit and tag.
#   scripts/release.sh BlackBox_980
# Then: git push origin main --tags  ->  GitHub Actions builds the installers into a draft release.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT="${1:?usage: scripts/release.sh BlackBox_NNN}"
cd "$ROOT"
[ -z "$(git status --porcelain)" ] || { echo "working tree not clean"; exit 1; }

.venv/bin/python fw/build.py --product "$PRODUCT" --release
REL=build/fw/release
VER=$(python3 -I -c 'import json,glob; m=sorted(glob.glob("build/fw/release/*.json"))[-1]; print(json.load(open(m))["version"])')
git rev-parse -q --verify "refs/tags/$VER" >/dev/null && { echo "tag $VER exists"; exit 1; }

find installer/assets/release -type f ! -name .keep -delete
cp "$REL/juicebox-$VER.json" "$REL/juicebox-$VER-app.bin" installer/assets/release/
(cd installer && go test ./...)

git add installer/assets/release
git commit -m "release: Juice-Box $VER ($PRODUCT)"
git tag -a "$VER" -m "Juice-Box $VER"
echo "ready: git push origin main $VER"
