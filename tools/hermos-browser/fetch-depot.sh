#!/usr/bin/env bash
# Download the depot archives the HermOS Browser is built from, verify every signature against the
# depot user's public key, and unpack them into PROJECT/var/depot (where Goa looks first), so Goa
# needs no network access of its own (its build sandbox has none).
# Usage: fetch-depot.sh PROJECT_DIR KEYS_DIR     (KEYS_DIR holds <user>/pubkey, e.g. genode/repos/gems/sculpt/depot)
set -euo pipefail
PROJECT=${1:?goa project dir}; KEYS=${2:?directory with <depot user>/pubkey}
DEPOT=$PROJECT/var/depot; URL=https://depot.genode.org
# each archive is verified against the key of ITS depot user only
verify() {   # user sig file
	local home; home=$(mktemp -d); chmod 700 "$home"
	GNUPGHOME=$home gpg --batch -q --import "$KEYS/$1/pubkey" 2>/dev/null
	GNUPGHOME=$home gpg --batch --verify "$2" "$3" 2>/dev/null; local rc=$?; rm -rf "$home"; return $rc
}
mkdir -p "$DEPOT"
get() {   # user/type/name/version
	local a=$1
	[ -d "$DEPOT/$a" ] && return 0
	local tmp; tmp=$(mktemp -d)
	curl -fsS -m 300 -o "$tmp/a.tar.xz"     "$URL/$a.tar.xz"
	curl -fsS -m 60  -o "$tmp/a.tar.xz.sig" "$URL/$a.tar.xz.sig"
	verify "${a%%/*}" "$tmp/a.tar.xz.sig" "$tmp/a.tar.xz" || { echo "BAD SIGNATURE: $a" >&2; exit 1; }
	mkdir -p "$DEPOT/$(dirname "$a")"
	tar -xJf "$tmp/a.tar.xz" -C "$DEPOT/$(dirname "$a")"      # the archive holds one top-level <version>/ directory
	rm -rf "$tmp"; echo "ok  $a"
}
while read -r a; do [ -n "$a" ] && get "$a"; done

# local API shims (no download): the ABI of libraries the depot ships no API archive for
for d in "$(dirname "$0")"/local-depot/*/api/*/*; do
	[ -d "$d" ] || continue
	rel=${d#"$(dirname "$0")"/local-depot/}
	mkdir -p "$DEPOT/$(dirname "$rel")"; rm -rf "$DEPOT/$rel"; cp -r "$d" "$DEPOT/$rel"; echo "ok  local $rel"
done
