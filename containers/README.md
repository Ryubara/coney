# Implementer containers

Engine code is written in containers that never see the original game or the analysis zone (see
[the clean room](../docs/clean-room.md), section 4). `coney-tools impl` drives them; this folder defines them.

| File | What it is |
| --- | --- |
| `Dockerfile` | The image: a pinned Ubuntu snapshot, the C++ toolchain (gcc, clang, CMake, Ninja), Mesa for software rendering, ffmpeg, Python with uv, Node, a pinned Claude Code and Tether with the containers' locked config (`impl build` adds both from the machine folder and refuses to build without them). Everything is baked in at build time: containers have no network beyond the API. |
| `compose.yml` | The pool, two containers per implementer: `impl-N-net` sets the egress wall and alone holds the network capabilities; `impl-N` joins its network once the wall reports healthy and runs as the user `impl` with no capabilities at all. |
| `init-firewall.sh` | The egress wall: default deny, DNS, and HTTPS to the hosts in `allowed-domains` only. It checks itself before reporting healthy. |
| `allowed-domains` | The only hosts an implementer may reach: the Anthropic API. |
| `managed-settings.json` | Claude Code's settings inside: telemetry, error reporting and updates off. |
| `session.sh` | Starts one task's session inside a container, with Tether loaded. |
| `system-prompt.txt` | The fixed instructions every implementer session starts with. |
| `gitconfig` | Commit signing with the container's own key. |

What makes a container a wall, each checked by `impl up` before anything starts and by `impl check` on running
containers: never privileged; no capabilities for the implementer; Docker's default seccomp filter (no new user
namespaces); `no-new-privileges`; a read-only root filesystem; no Docker socket; only the container's own volumes
mounted; CPU and memory caps; no route to any host but the API, and none to the Windows host's ports. Docker runs in
a WSL2 distro that has no Windows drives and cannot start Windows programs, so even a container escape reaches no
Windows folder.

Claude Code's own sandbox is switched off inside: it needs new namespaces, which Docker's default seccomp filter
refuses, and the container already gives what it would add.

Each container makes its Ed25519 commit key on first start; the key never leaves the container's volume, and its
public line goes into `allowed_signers` through a change the maintainers approve. Commits leave a container only by
being pushed to its own outbox inside the distro; `impl collect` fetches them into the public side's collecting
repository.
