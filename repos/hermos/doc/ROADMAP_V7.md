# HermOS V7 roadmap: all earlier capabilities, and Shiro Brain

Source of requirements: the owner's V3 brief ("Phase 4: Shiro Local Brain. Integrate llama.cpp. Build the
RAG pipeline so Shiro reads Genode docs/HermOS code to generate perfect C++") and the later instruction
"a local small LLM that thoroughly knows how HermOS works and REA, so it can create components".

## A. Capabilities of earlier versions that V7 does not have yet

| # | Capability (where it was specified or built) | State in V7 | Step |
|---|---|---|---|
| 1 | Hermes Core Director + workers + procedure registry (master prompt section 3, `hermes_director.run`) | SOURCE ONLY, not in the image | A1 |
| 2 | Agents & Workers pane shows the Director's real state (master prompt) | VISUAL only | A1 |
| 3 | Web Cell / HermOS Browser (V3 Phase 3) | PARTIAL, blocked: depot Qt binaries are for base API 2026-04-16 | A2 |
| 4 | Server edition: `hermos_server.run`, nginx-class workers, status page (HermOS-Server repo) | status page only | A3 |
| 5 | Dual-Cell system: System Cells vs REA Cells (ephemeral, sandboxed) | manifest kind `rea` validates; no REA Cell exists | A4 |
| 6 | Clean Room: Observer agent (REA) extracts specs, Builder agent writes C++ | ABSENT | A4 |
| 7 | The Vault (ported and AI-generated apps) | ABSENT | A4 |
| 8 | MyKee routes: BYOK, Master Proxy, local offline LLM | cloud route only | B3 |
| 9 | Shiro Local Brain (llama.cpp + RAG) | retrieval over `doc/*.md` only; no model | B |
| 10 | Approval of consequential agent actions, audit log | ABSENT | A1 |

Already carried over from V4 in V7: MyKee router, retrieval, Capability Broker, Kill Mode (Meta+K).

## B. Shiro Brain: design

* **Runtime**: llama.cpp (CPU, ggml) as a Genode libc component `shiro_brain`; threads via libc pthreads;
  model read into RAM through the VFS (no mmap); one request at a time.
* **Model**: a small instruction model that is good at code (0.5B to 1.5B parameters, Q4 GGUF, about
  0.4 to 1.1 GB). Needs RAM beyond today's 768 MB baseline: the component is sized from the model file.
* **Knowing HermOS**: (1) retrieval over docs, the status table, the Genode release notes and the HermOS
  source, rebuilt with every image; (2) a fixed system prompt that states the OS rules (Cells, theme.hid,
  Capability Broker, REA, Clean Room); (3) later, a LoRA fine-tune on the same material.
* **Creating components**: the model proposes files (C++, `target.mk`, manifest, run-script lines). They go
  to a REA Cell (no network, no disk write), are checked (manifest, HID syntax, static checks) and shown
  to the user. Building needs the Genode toolchain, so the first version exports to the Vault for a host
  build. Clean Room rule: the Builder never receives decompiled code, only the Observer's behaviour spec.
* **Routing**: MyKee tries the local route first, then the cloud routes. One request format for both.

## B-status

| Step | State |
|---|---|
| B1 llama.cpp port + Genode library (`ports/llama.port`, `lib/mk/llama.mk`) | DONE: builds and links |
| B2 `shiro_brain` component | DONE: loads a GGUF through the VFS and generates on NOVA (tested with a random dummy model, which produces meaningless text) |
| B3 MyKee local route | DONE: `local://shiro` is tried before the cloud; tested end to end (command bar, agent, router, brain, answer) |
| B4 corpus and system prompt | DONE: knowledge base of 203 facts, retrieval, facts-and-question prompt; system prompt file for a real base model |
| B5 real model | OPEN: a base GGUF is needed for reasoning. Shiro-Mini (trained here) covers answering questions about HermOS |
| B6a Forge: component proposal checker | DONE, host tested (14 cases) and boot tested |
| B6b Vault, approval step, REA Cell UI | TO DO |

Build with a model: `HERMOS_BRAIN_MODEL=/path/model.gguf tools/build-hermos-v5.sh OUT`. Without it the image is unchanged.
`tools/shiro-brain/make_dummy_model.py` writes the random test model.

## C. Order of work (one step at a time)

B1 port llama.cpp to Genode (compile) -> B2 `shiro_brain` component with a tiny test model (plumbing only)
-> B3 MyKee local route -> B4 corpus and system prompt -> B5 real model (needs weights) -> A1 -> A4 -> A3 -> A2.
