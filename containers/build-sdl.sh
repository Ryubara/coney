#!/bin/sh
# Builds SDL3 from its release archive, checked against its SHA-256, and installs it in /usr/local, where CMake finds
# it as SDL3: the one SDL build both the implementer image and CI's engine job use. A console build without X11 or
# Wayland, for machines with no display: its video drivers are offscreen and dummy, its GPU API runs on Vulkan.
#
#   build-sdl.sh VERSION SHA256   (the Dockerfile passes its ARGs)
#   build-sdl.sh                  (reads ARG SDL_VERSION and ARG SDL_SHA256 from the Dockerfile beside this script)
#
# Needs curl, cmake, ninja and a C compiler; run as root.
set -eu

if [ $# -eq 0 ]; then
    dockerfile="$(dirname "$0")/Dockerfile"
    set -- "$(sed -n 's/^ARG SDL_VERSION=//p' "$dockerfile")" "$(sed -n 's/^ARG SDL_SHA256=//p' "$dockerfile")"
fi
[ $# -eq 2 ] && [ -n "$1" ] && [ -n "$2" ] || { echo "usage: build-sdl.sh [VERSION SHA256]" >&2; exit 2; }
version=$1
sha256=$2

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSLo "$work/sdl.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-${version}/SDL3-${version}.tar.gz"
echo "${sha256}  $work/sdl.tar.gz" | sha256sum -c -
tar -xzf "$work/sdl.tar.gz" -C "$work"
cmake -S "$work/SDL3-${version}" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_OFFSCREEN=ON -DSDL_DUMMYVIDEO=ON \
    -DSDL_VULKAN=ON -DSDL_GPU=ON
cmake --build "$work/build"
cmake --install "$work/build"
ldconfig
