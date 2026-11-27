#!/usr/bin/env bash
#
# One-time GitHub cleanup for HermOS-Home-Pro (run on the laptop, from a clone
# of this repository with push access).
#
#   tools/github-cleanup.sh          dry run: prints what it would do
#   tools/github-cleanup.sh --apply  does it
#
# 1. Tags every old line as archive/<name>, so no commit is lost.
# 2. Pushes those tags and checks they arrived.
# 3. Only then deletes the old branches on GitHub.
#
# Afterwards GitHub has two branches: main and V9-beta.
# Nothing is force-pushed and no history is rewritten.
set -euo pipefail

apply=0
[[ ${1:-} == --apply ]] && apply=1
run() { echo "+ $*"; if (( apply )); then "$@"; fi; }

remote=origin
# old branch -> archive tag
declare -A lines=(
	[V5Beta]=archive/V5Beta
	[V6Beta]=archive/V6Beta
	[V7]=archive/V7
	[V7Beta]=archive/V7Beta
	[V8]=archive/V8
	[HermOS4]=archive/HermOS4
	[HermOS4-main]=archive/HermOS4
	[archive/hermos-server]=archive/hermos-server
	[v8-housekeeping]=archive/v8-housekeeping
)

git fetch --prune "$remote"

echo "== 1/3 tag old lines"
for b in "${!lines[@]}"; do
	t=${lines[$b]}
	if ! git rev-parse -q --verify "refs/remotes/$remote/$b" >/dev/null; then
		echo "   skip $b (no such branch on $remote)"; continue
	fi
	sha=$(git rev-parse "refs/remotes/$remote/$b")
	if git rev-parse -q --verify "refs/tags/$t" >/dev/null; then
		have=$(git rev-parse "refs/tags/$t^{commit}")
		[[ $have == "$sha" ]] || { echo "error: tag $t exists at $have, branch $b is at $sha" >&2; exit 1; }
		echo "   $t already at ${sha:0:7}"
	else
		run git tag -a "$t" "$sha" -m "Archived branch $b (${sha:0:7})"
	fi
done

echo "== 2/3 push tags"
tags=$(printf '%s\n' "${lines[@]}" | sort -u)
run git push "$remote" $(printf 'refs/tags/%s ' $tags)
if (( apply )); then
	for t in $tags; do
		git ls-remote --exit-code --tags "$remote" "refs/tags/$t" >/dev/null \
			|| { echo "error: tag $t is not on $remote; stopping before any branch is deleted" >&2; exit 1; }
	done
fi

echo "== 3/3 delete old branches on $remote"
for b in "${!lines[@]}"; do
	git rev-parse -q --verify "refs/remotes/$remote/$b" >/dev/null || continue
	run git push "$remote" --delete "refs/heads/$b"
done

if (( apply )); then
	echo "Done. Branches left on $remote:"; git ls-remote --heads "$remote"
else
	echo; echo "Dry run. Re-run with --apply to do it."
fi
