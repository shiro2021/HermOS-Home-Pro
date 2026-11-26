# HermOS Server — design (an nginx-class server on HermOS)

This is the design for the **HermOS Server** edition: a web/reverse-proxy
server built on HermOS (Genode/NOVA) with the Hermes agent as its director.
It maps the seven nginx principles (see `docs/nginx-architecture.md`) onto
HermOS's native primitives — one principle at a time, so nothing is
reinvented, only re-hosted.

## Mapping nginx → HermOS

| nginx | HermOS equivalent |
|---|---|
| master process | the **Hermes agent director** (control plane) |
| worker processes | **Genode components** — one sandboxed child per worker (data plane) |
| epoll/kqueue event loop | Genode **async signals** + **NIC session** (lwIP stack) + **VFS** |
| `ngx_module_t` + `ngx_commands` | HermOS **depot recipes / plugins** |
| `nginx.conf` (http→server→location) | a **server config ROM** (same pattern as `theme.hid` → `theme_engine`) |
| per-request pool | Genode **`Heap`/`Allocator` arenas** per request |
| HTTP 11-phase pipeline | the same **named-phase request pipeline** inside the server component |
| upstream pools | **multiple NIC sessions / child backend components** |

## Architecture

```
                      ┌─────────────────────────────────────┐
                      │        Hermes agent (director)       │  control plane:
                      │  spawn/reconfigure/stop workers,     │  = nginx master
                      │  publish server_config ROM           │
                      └───────────────┬─────────────────────┘
                                      │ report_rom / config ROM
        ┌─────────────────┬───────────┴──────────┬──────────────────┐
        ▼                 ▼                      ▼                  ▼
 ┌──────────────┐  ┌──────────────┐      ┌──────────────┐   ┌──────────────┐
 │ server worker│  │ server worker│ ...  │  static VFS  │   │  upstream    │
 │  (child 0)   │  │  (child 1)   │      │  file server │   │  backend(s)  │
 └──────┬───────┘  └──────┬───────┘      └──────────────┘   └──────────────┘
        └───────────┬─────┘
                    │ shared NIC (lwIP)  ← data plane: = nginx workers
```

### 1. Control plane / data plane (nginx: master/workers)

The **Hermes agent** is the master. It starts N `hermos_server` worker
components, passes them a `server_config` ROM (listen addrs, locations,
upstreams), and can reconfigure or respawn them live — exactly the nginx
graceful-reload pattern, but the "signal" is a report_rom config update
instead of a `SIGHUP`.

### 2. One async loop per worker (nginx: epoll)

Each `hermos_server` worker is a **single-threaded base component** that
loops on its **NIC session** (the lwIP socket layer) with edge-triggered
semantics. No per-connection threads. Connections are a state machine. This
is the same shape already proven in `hermes_agent_cpp` (which holds a lwIP
`Nic` session and waits on `ui_intent`).

### 3. Uniform module ABI (nginx: ngx_module_t)

A server module is a table of `{ directives, per-scope conf, phase
handlers, init/exit hooks }`. Modules are compiled into the worker (HermOS
has no dynamic linking in the hot path) and selected at build time — the
depot recipe is the "module registry".

### 4. Named request phases (nginx: 11 phases)

The worker's HTTP handler runs the identical ordered pipeline:
`post_read → server_rewrite → find_config → rewrite → preaccess → access →
precontent → content → log`. Static file serving, reverse proxy, gzip,
auth are all just phase handlers.

### 5. Hierarchical config (nginx: nginx.conf)

The `server_config` ROM is hierarchical (`server → location`), parsed by a
theme_engine-style parser that emits the absolute route table to the worker
— same decoupling the desktop already uses (`theme.hid` → `theme_engine` →
`desktop_builder`).

### 6. Arena allocation (nginx: pools)

Each request gets a `Genode::Heap` (or a sliced arena) freed in one shot at
`log` phase. No per-request malloc churn.

### 7. Upstreams (nginx: upstream pools)

Backends are either **external IPs** (via the lwIP NIC) or **local HermOS
children** (e.g. a `hermos_app` serving a service). Connection reuse + health
checks live in the worker.

## Build order (what ships first)

1. `hermos_server` — minimal HTTP/1.1 worker (parse request → route → serve
   a static file from the VFS), boot-verified headless.
2. `server_config` ROM + parser (hierarchical `server → location`).
3. Static content phase (VFS file serving) + access-log phase.
4. Reverse-proxy content phase (lwIP upstream) + keepalive.
5. Hermes-agent orchestration (spawn/reconfigure workers) + the server Cell
   in the desktop UI.

## Status

- [x] nginx architecture reverse-engineered (this repo, `docs/nginx-architecture.md`)
- [ ] `hermos_server` component skeleton
- [ ] config ROM parser
- [ ] static + proxy phases
- [ ] Hermes-agent orchestration
