# nginx architecture — reverse-engineered blueprint

Analysis target: **nginx 1.24.0** (Ubuntu `nginx` 1.24.0-2ubuntu7.19, x86-64, stripped)
Method: REA (Ghidra) binary analysis cross-checked against the GPL source
(`nginx-1.24.0/src`). What follows is the *architecture* — the design
principles a "perfect" server is built on — not a code dump.

## 1. Process model — master / workers

nginx runs as **one master process + N worker processes** (N = CPU count by
default). The split is strict:

- **Master** (`ngx_master_process_cycle`) is the *control plane*. It blocks a
  fixed signal set (`SIGCHLD`, `SIGALRM`, `SIGIO`, `SIGINT`, `SIGHUP`
  reconfigure, `SIGUSR1` reopen, `SIGUSR2` binary upgrade, `SIGQUIT` graceful
  shutdown, `SIGWINCH`/`SIGTERM`) and never serves traffic. Its only jobs:
  spawn workers, reap dead children, and translate signals into worker
  commands over a socketpair channel.
- **Workers** (`ngx_worker_process_cycle`) are the *data plane*. Each runs its
  own independent event loop and shares the listening sockets. There are no
  locks between workers — isolation comes from each worker owning its own
  connections. Graceful reload = spawn a new worker generation with the new
  config, then drain the old ones.

Design principle: **a small privileged supervisor + many independent,
lock-free event-loop engines.**

## 2. Event model — one thread, non-blocking, edge-triggered

Each worker is **single-threaded** and drives everything through an event
demultiplexer (`ngx_event_actions`): `epoll` on Linux (`EPOLLET` +
`EPOLLRDHUP`), `kqueue` on BSD, IOCP on Windows. A connection is a **state
machine** (`idle → reading → writing → keepalive`), not a thread. Because no
work blocks, one worker handles tens of thousands of concurrent connections.

The event abstraction (`ngx_event_t`, `ngx_connection_t`) is the same object
for sockets, timers, async file I/O, and the worker-to-master channel — one
uniform readiness mechanism.

Design principle: **async, non-blocking I/O with a single event loop per
worker — never a thread per connection.**

## 3. Module system — the extensibility core

Everything in nginx is a module described by one struct `ngx_module_t`:

```c
struct ngx_module_s {
    ngx_uint_t  ctx_index, index;
    char       *name;
    ngx_uint_t  version;
    const char *signature;      /* 35-bit feature-compat fingerprint */
    void       *ctx;            /* type-specific config context */
    ngx_command_t *commands;    /* config directives this module adds */
    ngx_uint_t  type;           /* CORE / EVENT / HTTP / STREAM / MAIL */
    ngx_int_t (*init_master)(...);
    ngx_int_t (*init_module)(...);
    ngx_int_t (*init_process)(...);
    ngx_int_t (*init_thread)(...);
    void      (*exit_thread)(...);
    void      (*exit_process)(...);
    void      (*exit_master)(...);
};
```

- `commands` extends the **config grammar** (each directive is a name + a
  setter callback + a placement flag: which `http/server/location` levels it
  is legal in).
- `ctx` is the type-specific per-module config struct (allocated per
  `main/srv/loc` level and merged hierarchically).
- The `signature` is a **compatibility fingerprint** (EPOLL, KQUEUE, SSL,
  GZIP, PCRE, threads, …) so a module compiled against a different feature set
  is refused at load.

Design principle: **a uniform plugin interface where a module contributes
config directives, per-scope state, and lifecycle hooks — never bespoke glue.**

## 4. HTTP request pipeline — the 11 phases

An HTTP request flows through a fixed sequence of phases, and a module
registers a handler at a specific phase (`ngx_http_core_main_conf_t.phases[]`):

```
POST_READ       read request headers
SERVER_REWRITE  URL rewrites at server scope
FIND_CONFIG     resolve the location{} block
REWRITE         URL rewrites at location scope
POST_REWRITE    enforce rewrite termination (avoid loops)
PREACCESS       pre-auth filters (limit_req, limit_conn)
ACCESS          auth decisions (allow/deny, auth_basic)
POST_ACCESS     satisfy any/all gating
PRECONTENT      try_files, mirror
CONTENT         the actual producer (static, proxy, fastcgi, …)
LOG             access-log emission
```

Design principle: **a fixed, ordered request pipeline — modules plug into
named phases, and the order of concerns (rewrite → auth → content → log) is
enforced by the framework, not by the modules.**

## 5. Configuration — hierarchical, merged per scope

`nginx.conf` is a hierarchy `main → http → server → location → if`.
Each module's `create_conf` allocates its own config struct at *each* level,
and `init_conf` merges parent→child. Directives are validated at parse time
against their legal placement. This is why config is declarative, predictable,
and cheap to reload.

## 6. Memory — per-connection and per-request pools

No general-purpose `malloc` in the hot path. Each connection and each request
allocates from its own **pool** (`ngx_pool_t`) — a bump arena freed in one
shot when the request/connection ends. This is what makes per-request
allocation effectively free and leak-proof.

## 7. Reverse proxy / upstreams

Backends are grouped into **upstream** pools with connection reuse
(keepalive to backends), health checks, and pluggable load-balancing
(round-robin, least_conn, ip_hash, hash, random). The proxy module is just
another CONTENT-phase handler — the pipeline does not care who produced the
body.

---

## The "perfection" recipe (distilled)

1. **Supervisor + N lock-free workers** (control plane / data plane split).
2. **One async event loop per worker** (edge-triggered epoll, no threads per
   connection).
3. **Uniform module ABI** (directives + per-scope state + lifecycle hooks).
4. **Fixed ordered request pipeline** (named phases, framework-enforced).
5. **Hierarchical merged config** (declarative, reloadable).
6. **Arena memory pools** (per-request allocation ~free).
7. **Pluggable upstreams** (proxy is just another phase handler).
