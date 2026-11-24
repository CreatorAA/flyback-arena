#!/usr/bin/env sh
set -eu
# Run on the build host. Creates a redistributable input cache; does not change system packages.
out=${1:?usage: prepare-linux.sh DIRECTORY}; mkdir -p "$out/sdk/include"; cd "$out"
. /etc/os-release
case "$ID:$VERSION_ID:$(dpkg --print-architecture)" in
 ubuntu:24.04:amd64|ubuntu:22.04:amd64|debian:12:amd64) ;;
 *) printf '%s\n' "Use a native libmsquic.so.2 package for $ID $VERSION_ID $(uname -m)." >&2; exit 1 ;;
esac
version=2.6.4; mdns=a569c4759bd47e0f2a7bfc4d4c19620445782806
curl --fail --location --output libmsquic.deb "https://packages.microsoft.com/$ID/$VERSION_ID/prod/pool/main/libm/libmsquic/libmsquic_${version}_amd64.deb"
for f in msquic.h msquic_posix.h quic_sal_stub.h; do
 curl --fail --location --output "sdk/include/$f" "https://raw.githubusercontent.com/microsoft/msquic/v$version/src/inc/$f"
done
curl --fail --location --output sdk/include/mdns.h "https://raw.githubusercontent.com/mjansson/mdns/$mdns/mdns.h"
printf '%s\n' "Cache ready at $(pwd). Install the native runtime with: sudo apt install $(pwd)/libmsquic.deb"
printf '%s\n' "CMake: -DFLYBACK_MSQUIC_SDK=$(pwd)/sdk -DFLYBACK_MDNS_INCLUDE=$(pwd)/sdk/include"
