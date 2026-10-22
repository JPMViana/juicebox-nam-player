#!/usr/bin/env bash
# Clone third-party reference repositories into reference/ (git-ignored).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/reference" && cd "$ROOT/reference"

clone() { [ -d "$2" ] || git clone --depth 1 "$1" "$2"; }

clone https://github.com/AL-255/FM-1-RE.git FM-1-RE
clone https://github.com/kagaimiq/jl-misctools.git jl-misctools
clone https://github.com/kagaimiq/jl-uboot-tool.git jl-uboot-tool
clone https://github.com/sdatkinson/NeuralAmpModelerCore.git NeuralAmpModelerCore
[ -d Felucca ] || git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/hugelton/Felucca.git Felucca
clone https://github.com/entitymar/MVAVE-ANNBLACKBOX-FIRMWARE-UPDATE-SOFTWARE-decompiled.git annblackbox-updater-decompiled
# Large (~GBs), clone manually when starting Phase 3:
#   git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git fw-AC79_AIoT_SDK
