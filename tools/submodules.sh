#!/usr/bin/env bash
# Fills a game checkout's submodules, all the way down, from a cache of bare mirrors that every checkout on the machine
# shares: what `git submodule update --init --recursive` does, without downloading them again. The first checkout on a
# machine fills the cache (the time a plain clone takes, most of it rt64's contrib/dxc); every one after it takes as
# long as writing out the files, and fetches only a submodule whose pinned commit the cache does not have yet.
#
#   bash lib/turok-engine/tools/submodules.sh [checkout]   # default: the current directory
#
# In a fresh game checkout, `git submodule update --init lib/turok-engine` first (one small repository), or run this
# from any turok-engine checkout with the game's path. Run it again after moving the game's branch, as the plain
# command: it puts every submodule at the commit the checkout pins, lib/turok-engine included.
#
# TUROK_GIT_CACHE: where the mirrors live (default ~/.cache/turok-git), one bare repository per submodule URL.
# A checkout reads its submodules' objects from the cache (objects/info/alternates) and copies none of them, so the
# cache is never pruned or garbage collected, and deleting it breaks the checkouts filled from it. Any run may add to it
# (a missing mirror, a fetch); nothing else does, so nobody has to refresh it. Runs at once are safe: a mirror is made
# under another name and renamed into place, and git's own locks cover fetches.
set -euo pipefail
shopt -s inherit_errexit   # a failure inside $(mirror ...) stops the run too

cache="${TUROK_GIT_CACHE:-$HOME/.cache/turok-git}"
g() { git -c core.longpaths=true "$@"; }   # paths past Windows' 260 characters

# mirror <url> <commit>: prints the url's mirror, made if missing and fetched if it lacks the commit. Prints nothing
# for a url that is not a remote one (a local path), which then clones as it would without the cache.
mirror() {
	local url=$1 sha=$2 key dir tmp
	case $url in *://* | *@*:*) ;; *) return 0 ;; esac
	key=${url#*://}; key=${key#*@}; key=${key//:/\/}; key=${key%/}; key=${key%.git}
	dir="$cache/$key.git"
	if [ ! -d "$dir" ]; then
		echo "submodules: caching $url" >&2
		mkdir -p "${dir%/*}"
		tmp="$dir.tmp-$$"
		rm -rf "$tmp"
		g clone --bare --quiet "$url" "$tmp"
		g -C "$tmp" config remote.origin.fetch '+refs/heads/*:refs/heads/*'
		g -C "$tmp" config gc.auto 0           # checkouts borrow its objects: none may ever be pruned
		g -C "$tmp" config maintenance.auto false
		# Windows refuses to rename a directory while anything (a virus scanner) holds a file in it open: try again.
		for _ in 1 2 3 4 5 6 7 8 9 10; do
			mv -T "$tmp" "$dir" 2>/dev/null && break
			[ -d "$dir" ] && { rm -rf "$tmp"; break; }   # another run made it first: keep theirs
			sleep 3
		done
		[ -d "$dir" ] || { echo "submodules: could not rename $tmp to $dir" >&2; return 1; }
	fi
	if ! g -C "$dir" cat-file -e "$sha^{commit}" 2>/dev/null; then
		echo "submodules: fetching $url" >&2
		g -C "$dir" fetch --quiet --tags origin || g -C "$dir" fetch --quiet --tags origin ||
			echo "submodules: fetching $url failed; its checkout fetches what it lacks itself" >&2
	fi
	echo "$dir"
}

# fill <repo>: the repo's submodules, each cloned with its mirror as reference, then theirs.
fill() {
	local repo=$1 entry name path url sha ref
	[ -f "$repo/.gitmodules" ] || return 0
	g -C "$repo" submodule init --quiet   # urls into the config, relative ones resolved
	local entries=()
	mapfile -t entries < <(g config -f "$repo/.gitmodules" --get-regexp '^submodule\..*\.path$')
	for entry in "${entries[@]}"; do
		name=${entry%% *}; name=${name#submodule.}; name=${name%.path}
		path=${entry#* }
		[ "$(g config -f "$repo/.gitmodules" --default checkout "submodule.$name.update")" = none ] && continue
		sha=$(g -C "$repo" ls-files -s -- "$path" | awk '$1 == "160000" { print $2 }')
		[ -n "$sha" ] || continue   # named in .gitmodules, absent from the tree
		# .gitmodules' url, as a fresh clone has it: a shared clone's config can name a local path its worktrees lack.
		url=$(g config -f "$repo/.gitmodules" "submodule.$name.url")
		case $url in ./* | ../*) url=$(g -C "$repo" config "submodule.$name.url") ;; esac   # resolved by init
		ref=$(mirror "$url" "$sha")
		g -C "$repo" -c "submodule.$name.url=$url" submodule update --quiet ${ref:+--reference "$ref"} -- "$path"
		fill "$repo/$path"
	done
}

start=$SECONDS
fill "${1:-.}"
echo "submodules: done in $((SECONDS - start)) s (cache: $cache)" >&2
