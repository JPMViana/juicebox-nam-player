#!/usr/bin/env bash
# Run a JieLi toolchain binary (pi32v2 clang, objdump, ...) through Docker on macOS.
#   scripts/pi32cc.sh clang -O2 -S foo.c -o foo.s      (clang gets -target pi32v2)
#   scripts/pi32cc.sh objdump -d foo.o
# Paths must be inside the project. JIELI_TOOLCHAIN defaults to .toolchain/toolchain.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${JIELI_TOOLCHAIN:-$ROOT/.toolchain/toolchain}"
tool="$1"; shift
case "$tool" in
    clang)   bin=pi32v2/bin/clang; set -- -target pi32v2 "$@" ;;
    objdump|objcopy|nm|ld|ar) bin=pi32v2/bin/$tool ;;
    *)       bin=$tool ;;
esac
rel="$(python3 -c 'import os,sys;print(os.path.relpath(sys.argv[1],sys.argv[2]))' "$PWD" "$ROOT")"
exec docker run --rm --platform linux/amd64 -v "$ROOT:/work" -v "$(cd "$TC" && pwd -P):/opt/jieli:ro" \
    -w "/work/$rel" debian:bookworm-slim "/opt/jieli/$bin" "$@"
