#!/usr/bin/env bash
# Fetch the third-party sources at pinned versions and verify the exact commits.
# Nothing third-party is committed to this repository.
set -euo pipefail
DEPS=${1:?usage: fetch-deps.sh DEPS_DIR}
mkdir -p "$DEPS"
fetch() {  # name url tag commit
	local name=$1 url=$2 tag=$3 want=$4
	if [ ! -d "$DEPS/$name/.git" ]; then
		git clone -q --depth 1 --branch "$tag" "$url" "$DEPS/$name"
	fi
	local got; got=$(git -C "$DEPS/$name" rev-parse HEAD)
	if [ "$got" != "$want" ]; then
		echo "ERROR: $name $tag is at $got, expected $want" >&2; exit 1
	fi
	echo "ok  $name $tag $got"
}
fetch imgui https://github.com/ocornut/imgui   v1.91.9 97428e8ac99e339ce05eee531cf55b77b29ea709
fetch glfw  https://github.com/glfw/glfw       3.4     7b6aead9fb88b3623e3b3725ebb42670cbe4c579
fetch json  https://github.com/nlohmann/json   v3.11.3 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
