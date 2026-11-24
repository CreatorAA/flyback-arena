#!/bin/sh
# From a completed release build on Ubuntu 24.04 amd64:
# sh native/game/package-linux.sh BUILD /path/to/libmsquic_2.6.4_amd64.deb OUTPUT.tar.gz
set -eu
test "$#" -eq 3 || { echo 'Usage: package-linux.sh BUILD MSQUIC.deb OUTPUT.tar.gz' >&2; exit 2; }
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
build=$(realpath "$1"); deb=$(realpath "$2"); out=$(realpath -m "$3")
test "$(dpkg-deb -f "$deb" Package)" = libmsquic
test "$(dpkg-deb -f "$deb" Version)" = 2.6.4
test "$(dpkg-deb -f "$deb" Architecture)" = amd64
stage=$(mktemp -d)
source_tmp=$(mktemp -d)
trap 'rm -rf -- "$stage" "$source_tmp"' EXIT
cmake --install "$build" --prefix "$stage"
for path in arena-server flyback-bridge data/bots/arena-bot-guard run-delegated; do test -x "$stage/$path"; done
for path in server.toml admin-web/app.js README-Linux.md examples/flyback.service; do test -f "$stage/$path"; done
mkdir -p "$stage/runtime" "$stage/licenses" "$stage/bot-source"
install -m 644 "$deb" "$stage/runtime/libmsquic_2.6.4_amd64.deb"
install -m 644 "$repo/LICENSE" "$stage/LICENSE.txt"
for notice in "$repo"/flyback/packaging/licenses/*; do
    case "${notice##*/}" in combat-audio.txt) continue;; esac
    install -m 644 "$notice" "$stage/licenses/"
done
install -m 644 /usr/share/common-licenses/LGPL-2.1 "$stage/licenses/glibc-LGPL-2.1.txt"
for pkg in libc6 libc6-dev libstdc++-13-dev libgcc-13-dev; do
    install -m 644 "/usr/share/doc/$pkg/copyright" "$stage/licenses/$pkg-copyright.txt"
done
dpkg-query -W libc6 libc6-dev gcc-13-base g++-13 libstdc++-13-dev libgcc-13-dev libssl3t64 libzstd1 libargon2-1 libnuma1 libmsquic > "$stage/licenses/Ubuntu-package-versions.txt"
cp "$repo/flyback/native/game/dart.hpp" "$stage/bot-source/"
for prog in "$stage"/data/bots/*; do
    test -x "$prog" || continue
    id=$(basename "$prog"); test "$id" != arena-bot-guard || continue
    mkdir "$stage/bot-source/$id"
    cp "$repo/boomerang/bots/$id/player.cpp" "$stage/bot-source/$id/"
done
test "$(find "$stage/bot-source" -name player.cpp | wc -l)" -eq 13
# libc6-dev supplies libc.a. Package on the bot build host before upgrading its toolchain.
glibc_version=$(dpkg-query -W -f='${source:Version}' libc6-dev:amd64)
test -n "$glibc_version"
if [ -n "${FLYBACK_GLIBC_SOURCE_DIR:-}" ]; then
    srcdir=$(realpath "$FLYBACK_GLIBC_SOURCE_DIR")
else
    srcdir=$source_tmp
    (cd "$srcdir" && apt-get source --download-only --only-source "glibc=$glibc_version")
fi
python3 "$repo/flyback/packaging/stage-glibc-source.py" "$srcdir" "$stage/bot-source/glibc" "$glibc_version"
tar -czf "$out" -C "$stage" .
