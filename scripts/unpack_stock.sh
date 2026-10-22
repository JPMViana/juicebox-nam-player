#!/usr/bin/env bash
# Download the factory V20 firmware and unpack it with jl-misctools.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware/stock/BlackBox_FACTORY_V20.fwsc"
SHA=c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08
URL=https://raw.githubusercontent.com/entitymar/annblackbox-custom-firmware-installer/main/firmware/BlackBox_FACTORY_V20.fwsc

mkdir -p "$(dirname "$FW")" "$ROOT/analysis"
[ -f "$FW" ] || curl -sL -o "$FW" "$URL"
echo "$SHA  $FW" | shasum -a 256 -c -

[ -d "$ROOT/.venv" ] || python3 -m venv "$ROOT/.venv"
"$ROOT/.venv/bin/pip" -q install pyyaml crcmod pycryptodome

WORK="$ROOT/analysis/v20_input"
mkdir -p "$WORK" && cp "$FW" "$WORK/"
cd "$ROOT/reference/jl-misctools/firmware"
"$ROOT/.venv/bin/python" fwunpack_newfw.py --dirname "$ROOT/analysis/v20_unpack" "$WORK/$(basename "$FW")"
rm -rf "$WORK"
strings -n 5 "$ROOT/analysis/v20_unpack/files/app.bin" > "$ROOT/analysis/v20_app_strings.txt"
echo "Unpacked to analysis/v20_unpack"
