#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The link rule of azahar_ps5_libraries (CMakeLists.txt): writes the archives CMake would link, one
# absolute path a line, into LIST, and touches TARGET so the build sees it done.
#
#   record-libraries.sh LIST TARGET LIBRARIES...
#
# Paths relative to the build directory (where ninja runs the rule) are made absolute; the compiler
# driver's options and the system libraries (-l..., which the app's link names itself) are left out.

set -euo pipefail
list=$1 target=$2
shift 2
: >"$list.tmp"
for library in "$@"; do
    case $library in
        -* | "") ;;
        /*) echo "$library" >>"$list.tmp" ;;
        *) echo "$PWD/$library" >>"$list.tmp" ;;
    esac
done
mv "$list.tmp" "$list"
touch "$target"
