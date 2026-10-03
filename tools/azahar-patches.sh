#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to Azahar's own files (Mihawk's PS5_Azahar), kept as patches/azahar/*.patch
# against the pinned commit.
#
#   tools/azahar-patches.sh apply    put them on a branch named ps5 in .deps/PS5_Azahar (once)
#   tools/azahar-patches.sh export   write the ps5 branch's commits back to patches/azahar
#
# To change Azahar: edit .deps/PS5_Azahar on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
azahar=$PS5CEMU_ROOT/.deps/PS5_Azahar
patches=$PS5CEMU_ROOT/patches/azahar
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "azahar"))' \
    "$PS5CEMU_ROOT/tools/deps.json")
identity=(-c user.name=ps5cemu -c user.email=ps5cemu@localhost)

case ${1:-} in
apply)
    if git -C "$azahar" rev-parse -q --verify refs/heads/ps5 >/dev/null; then
        exit 0 # already there (and possibly being worked on)
    fi
    git -C "$azahar" checkout -q -b ps5 "$pin"
    shopt -s nullglob
    files=("$patches"/*.patch)
    if ((${#files[@]})); then
        git "${identity[@]}" -C "$azahar" am -q --keep-cr "${files[@]}"
    fi
    echo "==> [azahar] applied ${#files[@]} patches on branch ps5"
    ;;
export)
    [[ -z $(git -C "$azahar" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/PS5_Azahar first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$azahar" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/azahar-patches.sh apply|export" >&2
    exit 2
    ;;
esac
