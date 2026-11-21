#!/usr/bin/env bash
# Build the Hermes Agent desktop program for Linux and Windows (x86-64).
#   tools/hermes-desktop/build.sh [OUTDIR]        default OUTDIR: ./dist
# Needs: cmake, g++, libcurl and X11/GL development packages (Linux build),
#        g++-mingw-w64-x86-64-posix (Windows build), python3, git.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mkdir -p "${1:-$HERE/dist}" && cd "${1:-$HERE/dist}" && pwd)
WORK=${HERMES_WORK:-$HERE/build}
DEPS=${HERMES_DEPS:-$WORK/deps}
FONT=${HERMES_FONT:-$HERE/../../repos/hermos/src/app/desktop_builder/assets/Inter-Regular.ttf}
JOBS=${JOBS:-$(nproc)}

"$HERE/fetch-deps.sh" "$DEPS"

build_target() {   # show the compiler output when (and only when) the build fails
	local log; log=$(mktemp)
	if ! cmake --build "$1" -j"$JOBS" --target hermes-agent agent_test >"$log" 2>&1; then
		tail -60 "$log" >&2; rm -f "$log"; echo "BUILD FAILED in $1" >&2; exit 1
	fi
	rm -f "$log"
}

echo "== Linux x86-64"
cmake -S "$HERE" -B "$WORK/linux" -DHERMES_DEPS="$DEPS" -DHERMES_FONT="$FONT" -DCMAKE_BUILD_TYPE=Release >/dev/null
build_target "$WORK/linux"
"$WORK/linux/agent_test"
cp "$WORK/linux/hermes-agent" "$OUT/hermes-agent-linux-x86_64"
strip "$OUT/hermes-agent-linux-x86_64"

echo "== Windows x86-64 (MinGW-w64)"
cmake -S "$HERE" -B "$WORK/windows" -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/mingw-w64.cmake" \
      -DHERMES_DEPS="$DEPS" -DHERMES_FONT="$FONT" -DCMAKE_BUILD_TYPE=Release >/dev/null
build_target "$WORK/windows"
cp "$WORK/windows/hermes-agent.exe" "$OUT/hermes-agent-windows-x86_64.exe"
x86_64-w64-mingw32-strip "$OUT/hermes-agent-windows-x86_64.exe"

( cd "$OUT" && sha256sum hermes-agent-linux-x86_64 hermes-agent-windows-x86_64.exe > SHA256SUMS )
ls -la "$OUT"
