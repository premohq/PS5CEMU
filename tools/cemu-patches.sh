#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to Cemu's own files, kept as patches/cemu/*.patch against the pinned commit.
#
#   tools/cemu-patches.sh apply    put them on a branch named ps5 in .deps/Cemu (once)
#   tools/cemu-patches.sh export   write the ps5 branch's commits back to patches/cemu
#
# To change Cemu: edit .deps/Cemu on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cemu=$PS5CEMU_ROOT/.deps/Cemu
patches=$PS5CEMU_ROOT/patches/cemu
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "cemu"))' \
    "$PS5CEMU_ROOT/tools/deps.json")
identity=(-c user.name=ps5cemu -c user.email=ps5cemu@localhost)

case ${1:-} in
apply)
    if git -C "$cemu" rev-parse -q --verify refs/heads/ps5 >/dev/null; then
        exit 0 # already there (and possibly being worked on)
    fi
    git -C "$cemu" checkout -q -b ps5 "$pin"
    shopt -s nullglob
    files=("$patches"/*.patch)
    if ((${#files[@]})); then
        git "${identity[@]}" -C "$cemu" am -q --keep-cr "${files[@]}"
    fi
    echo "==> [cemu] applied ${#files[@]} patches on branch ps5"
    ;;
export)
    [[ -z $(git -C "$cemu" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/Cemu first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$cemu" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/cemu-patches.sh apply|export" >&2
    exit 2
    ;;
esac
