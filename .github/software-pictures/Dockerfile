# The container image the `software-pictures` job draws Coney's synthetic scenes in (.github/workflows/build.yml,
# docs/guides/testing.md#software-pictures). A picture depends on the OpenGL driver, so the driver is pinned three ways,
# and nothing the runner image brings along (its own Mesa moves with the image) is used:
#
#   - the base image by its digest, not by its tag;
#   - the package archive by a snapshot date, so every package version below stays installable for ever (Ubuntu's
#     archive keeps only the newest version of a package, and a pinned version would otherwise vanish);
#   - Mesa and the LLVM that llvmpipe compiles its shaders with by exact package version, checked at the end of the
#     build so that a pin the archive does not hold fails the build instead of installing something else.
#
# Moving any pin is a pull request of its own: it regenerates tests/pictures/reference/ in the same pull request and
# reports the old and new counts (`coney-tools picture scenes --update`, docs/guides/testing.md#software-pictures).
# Mesa 25.2.8 and LLVM 20.1.2 are what Ubuntu 24.04 shipped on 2026-10-29.
FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

ARG SNAPSHOT=20261029T000000Z
ARG MESA=25.2.8-0ubuntu0.24.04.4
ARG GLAPI=24.2.8-1ubuntu1~24.04.1
ARG LLVM=1:20.1.2-0ubuntu1~24.04.3

ENV DEBIAN_FRONTEND=noninteractive

# The base image has no ca-certificates, and the snapshot service answers every http request with a redirect to https
# (the first CI run of this job failed on that), so the certificates come first, from the image's own package source.
# They are not part of the pin: they only let apt open the https snapshot, and every package that matters is checked
# against the archive's signing key below.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Read the packages from the snapshot of that day, not from the live archive. Signed-By keeps the archive's own signing
# key (the keyring the base image ships) as the one that vouches for every index, and every package is checked against
# its index's hashes.
RUN printf 'Types: deb\nURIs: https://snapshot.ubuntu.com/ubuntu/%s\nSuites: noble noble-updates noble-security\nComponents: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' "$SNAPSHOT" \
      > /etc/apt/sources.list.d/ubuntu.sources \
    && rm -f /etc/apt/sources.list

# Mesa's GLX and EGL, its software driver and LLVM at the pinned versions; Xvfb (the virtual display) and the X11
# and xkb libraries SDL3 loads when it opens a window; the rest is whatever they depend on.
RUN apt-get -o Acquire::Check-Valid-Until=false update \
    && apt-get install -y --no-install-recommends \
         "libgl1-mesa-dri=$MESA" "libglx-mesa0=$MESA" "libegl-mesa0=$MESA" "libgbm1=$MESA" "mesa-libgallium=$MESA" \
         "libglapi-mesa=$GLAPI" "libllvm20=$LLVM" \
         libgl1 libegl1 libx11-6 libxext6 libxcursor1 libxfixes3 libxi6 libxrandr2 libxss1 libxtst6 libxkbcommon0 \
         xvfb xauth \
    && rm -rf /var/lib/apt/lists/* \
    && test "$(dpkg-query -W -f='${Version}' libgl1-mesa-dri)" = "$MESA" \
    && test "$(dpkg-query -W -f='${Version}' mesa-libgallium)" = "$MESA" \
    && test "$(dpkg-query -W -f='${Version}' libllvm20)" = "$LLVM"

# Mesa's software rasteriser for OpenGL, whatever the (virtual) display offers; SDL's silent audio driver.
ENV LIBGL_ALWAYS_SOFTWARE=1 \
    GALLIUM_DRIVER=llvmpipe \
    SDL_AUDIODRIVER=dummy
