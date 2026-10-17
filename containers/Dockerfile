# The implementer image: a toolchain for Coney's engine (C++23 with GCC and Clang, CMake, Ninja, Catch2 3) and Claude
# Code, nothing else. SDL3 is built from source (Ubuntu 24.04 does not package it) for a headless container: its
# offscreen and dummy video drivers, Vulkan and the GPU API, with Mesa's lavapipe as the Vulkan device.
# Built by `coney-tools impl build`. Everything is fixed here so a rebuild gives the same image: the base image and
# uv by digest, Ubuntu packages from one dated snapshot of the archive, Node and SDL3 by their checksums, Claude Code
# by version.
# Nothing is fetched when a container runs: its firewall lets it reach the Anthropic API only.

ARG UBUNTU=ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
ARG UV=ghcr.io/astral-sh/uv:0.12.21@sha256:a7aed3216253ee804de3e2d8afa5073baa1a177335345d43845cd4165e43b711

FROM ${UV} AS uv

FROM ${UBUNTU}

# The Ubuntu archive as it stood at this moment (snapshot.ubuntu.com): every package version follows from it.
ARG SNAPSHOT=20261001T000000Z
ARG NODE_VERSION=24.21.0
ARG NODE_SHA256=fd8e59d5a511510f6a298afb548f18c7d2b1be404d8b4a27d94fbe49f56cb2d6
ARG CLAUDE_CODE_VERSION=2.1.292
ARG SDL_VERSION=3.4.18
ARG SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
# The implementer's user id; coney-tools reads it from the label below.
ARG IMPL_UID=1500

SHELL ["/bin/bash", "-euo", "pipefail", "-c"]
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# The base image has no CA certificates, so the snapshot's first fetch skips the TLS peer check: apt still checks
# every index against the archive's signing key, which is what makes the packages trustworthy.
RUN rm -f /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list \
    && printf '%s\n' 'Types: deb' "URIs: https://snapshot.ubuntu.com/ubuntu/${SNAPSHOT}" \
        'Suites: noble noble-updates noble-security' 'Components: main universe' \
        'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' >/etc/apt/sources.list.d/snapshot.sources \
    && apt-get -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends ca-certificates \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        catch2 \
        clang \
        cmake \
        curl \
        ffmpeg \
        g++ \
        gcc \
        git \
        ipset \
        iptables \
        less \
        libgl1-mesa-dri \
        libvulkan1 \
        make \
        mesa-vulkan-drivers \
        ninja-build \
        openssh-client \
        pkg-config \
        python3 \
        python3-venv \
        ripgrep \
        socat \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

COPY --from=uv /uv /uvx /usr/local/bin/

# SDL3 in /usr/local, by the same script CI's engine job runs: windows are offscreen and a test reads frames back.
COPY --chmod=0755 build-sdl.sh /tmp/build-sdl.sh
RUN /tmp/build-sdl.sh "${SDL_VERSION}" "${SDL_SHA256}" && rm /tmp/build-sdl.sh

# Node (for npm, which installs Claude Code) from nodejs.org, checked against its published SHA-256.
RUN curl -fsSLo /tmp/node.tar.xz "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 --no-same-owner \
    && rm /tmp/node.tar.xz \
    && npm install -g --no-fund --no-audit "@anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}" \
    && npm cache clean --force \
    && claude --version

# The implementer: not root, no password, no sudo. Its folders exist in the image with its ownership, so a fresh
# named volume mounted there starts owned by it.
RUN userdel -r ubuntu 2>/dev/null || true \
    && useradd --uid "${IMPL_UID}" --user-group --create-home --shell /bin/bash impl \
    && install -d -o impl -g impl -m 0755 /work /work/coney /work/artifacts /outbox /home/impl/.claude \
    && install -d -o root -g root -m 0755 /work/task /etc/coney /etc/claude-code \
    && install -d -o impl -g impl -m 0700 /home/impl/.ssh
LABEL org.coney.impl-uid="${IMPL_UID}"

COPY --chmod=0644 allowed-domains /etc/coney/allowed-domains
COPY --chmod=0644 system-prompt.txt /etc/coney/system-prompt.txt
COPY --chmod=0644 managed-settings.json /etc/claude-code/managed-settings.json
COPY --chmod=0644 gitconfig /etc/gitconfig
COPY --chmod=0755 init-firewall.sh /usr/local/sbin/coney-firewall
COPY --chmod=0755 session.sh /usr/local/bin/coney-session

# Tether (coney-tools tether): the installed build and the containers' locked config, which `impl build` adds to the
# context from the machine folder. Every session loads it (session.sh); TETHER_CONFIG names its config, and Tether
# turns everything off when that file cannot be read.
COPY tether /opt/tether
COPY --chmod=0644 tether.json /etc/coney/tether.json
ENV TETHER_CONFIG=/etc/coney/tether.json

WORKDIR /work
