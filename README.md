# HermOS Server

HermOS is a Genode/NOVA-based operating system directed by the Hermes agent.
This is the **Server** edition — an nginx-class web/reverse-proxy server.

## What's here

| Path | Contents |
|---|---|
| `docs/nginx-architecture.md` | Reverse-engineered blueprint of nginx (the reference server) |
| `docs/hermos-server-design.md` | The HermOS Server design — nginx's principles mapped onto Genode + the Hermes agent |

## The idea in one line

nginx's "perfection" is a supervisor + N lock-free event-loop workers, a
uniform module ABI, and a fixed 11-phase request pipeline. HermOS Server
re-hosts exactly that shape on Genode: the **Hermes agent is the master**,
each **`hermos_server` worker is a sandboxed Genode child** driving an async
**NIC session** (lwIP), and config is a hierarchical ROM parsed the same way
the desktop consumes `theme.hid`.

## Status

- [x] nginx architecture reverse-engineered (REA + GPL source cross-check)
- [ ] `hermos_server` component skeleton
- [ ] config ROM parser
- [ ] static + proxy content phases
- [ ] Hermes-agent orchestration
