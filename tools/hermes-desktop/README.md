# Hermes Agent for desktop (Linux and Windows)

A small desktop program with the behaviour of the HermOS agent (`hermes_agent_cpp`): you type a
prompt, it asks the DeepSeek chat API, and shows the reply. One C++ code base, Dear ImGui on GLFW +
OpenGL; the HermOS look (orange accent when focused, carbon gray when not).

## Build

```bash
tools/hermes-desktop/build.sh [OUTDIR]     # default: tools/hermes-desktop/dist
```

Produces `hermes-agent-linux-x86_64` and `hermes-agent-windows-x86_64.exe` (cross-compiled with
MinGW-w64, statically linked) plus `SHA256SUMS`. It runs the core tests for both.

Needs: `cmake g++ python3 git`, `libcurl4-openssl-dev libgl-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev` (Linux build), `g++-mingw-w64-x86-64-posix` (Windows build).
Third-party sources (Dear ImGui v1.91.9, GLFW 3.4, nlohmann/json v3.11.3) are fetched at pinned tags and
the exact commits are verified (`fetch-deps.sh`); none is committed here. The Inter font (SIL OFL 1.1)
is embedded from `repos/hermos/src/app/desktop_builder/assets`.

## Run

- Linux needs `libcurl`, `libGL` and `libX11` (any desktop distribution has them). Windows needs nothing
  beyond Windows 10 or later (WinHTTP, OpenGL).
- **Key:** `HERMOS_MYKEE_KEY` (the key), or `HERMOS_MYKEE_KEY_FILE` (path to a file holding it), or the
  per-user file (`~/.config/hermos/mykee.key`, `%APPDATA%\hermos\mykee.key`). The **Key** button enters a
  key for the session or saves it to that file (owner-only on Linux).
- **Theme:** an optional `hermes-agent.theme` next to the program (or in the per-user folder) overrides
  any colour or the border/chamfer/glow values; `key: R,G,B` or `key: number`, `#` comments.

## Security

- The server certificate and host name are always verified (libcurl with the system CA store on Linux,
  WinHTTP with the Windows store on Windows). There is no switch to turn this off and no fallback.
- https only, no redirects, replies capped at 1 MiB, prompts at 16 KiB.
- A key must be one URL-safe token (16 to 256 characters); anything else counts as no key and is never sent.
- Each prompt is sent on its own; nothing is written to disk except a key you choose to save.

## Tests

`agent_test` (built with both targets) covers the key check, log excerpts, request/reply handling,
the key file (permissions, precedence) and the refusal of non-https URLs. It needs no network.
