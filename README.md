# HermOS Home Pro

HermOS is a Genode/NOVA-based operating system directed by the Hermes agent.
This is the **Home Pro** edition (consumer desktop), and the single repository for all HermOS work.

Current release line: **V9 Beta** — see [`dist/RELEASE_NOTES_V9_BETA.md`](dist/RELEASE_NOTES_V9_BETA.md).

## Layout

| Path | Contents |
|---|---|
| `repos/hermos/` | HermOS components — `desktop_builder`, `theme_engine`, `hermos_files`, `agents_sidebar`, `hermes_agent`, `shiro_brain`, `mykee_router`, support libs, llama.cpp port |
| `tools/` | Build, release, QEMU automation, Shiro Brain training and evaluation |
| `docs/` | Design documents, concept art, server design (`docs/server/`) |
| `dist/` | Release README and release notes (ISO files are on the Releases page, not in git) |

## Build a release

    HERMOS_GENODE_DIR=<Genode 26.08 tree> HERMOS_BRAIN_MODEL=<shiro-mini.gguf> tools/build-hermos-v9-beta.sh

## Releases

Bootable ISO images: https://github.com/shiro2021/HermOS-Home-Pro/releases

## Branches and history

- `main` — default branch, newest release line.
- `V9-beta` — V9 Beta release branch.
- `archive/*` tags — earlier lines (V5Beta, V6Beta, V7, V7Beta, V8, HermOS4) and the former HermOS-Server
  repository (`archive/hermos-server`). Nothing was deleted from history.

Former repositories, now contained here: HermOS-V4beta (in V8), hermos5 (V5Beta bundle, in V8),
HermOS-Private (identical to the first import), HermOS-Server (`archive/hermos-server`).
