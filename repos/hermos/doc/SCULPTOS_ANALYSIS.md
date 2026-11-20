# Genode SculptOS — architecture analysis for HermOS

Status: analysis of SculptOS 25.04/25.10 and the Genode 26.08 tree that HermOS
is built from. Every claim below is either taken from the official SculptOS
documentation (genode.org/documentation/articles/sculpt-25-04, `sculpt-25-04.pdf`)
or read from the source tree at
`/home/shiro/HermOS/genode-main/genode-26.08` (git `d18ee9e`). File paths are
given so each statement can be re-checked.

---

## 1. What SculptOS is

SculptOS is a component-based general-purpose desktop operating system built on
the Genode OS Framework. Its name describes the interaction model: the user
composes ("sculpts") a running system out of components while it is running.
The boot image is simultaneously a live system, a rescue system and a bootstrap
system (`repos/gems/run/sculpt.run`, documentation §Introduction).

Two properties matter more than any individual feature:

1. **The system is data, not a program.** What runs is decided by an init
   configuration document that can be rewritten at runtime.
2. **Authority is a capability.** A component can only reach what its config
   explicitly routes to it; there is no ambient "root".

Everything else in Sculpt follows from these two.

## 2. Boot architecture

The x86 image is a hybrid BIOS/UEFI image produced by `run/sculpt`:

| Stage | Component | Notes |
| --- | --- | --- |
| Firmware | UEFI (default) or legacy BIOS | Secure boot must be off; the image is unsigned |
| Bootloader | GRUB2 | menu entry `'Genode on NOVA'`, `multiboot2 /boot/bender` |
| Multiboot shim | `bender` | loads the kernel + `image.elf.gz`, passes `serial`, `serial_fallback`, `intel_hwp_performance` |
| Microkernel | NOVA (`hypervisor`) | args `iommu_intel iommu_amd serial` |
| Root component | `core` | capability/PD/RM/CPU/IRQ/IO_MEM/IO_PORT services, ROM from the boot modules |
| Init | `init` | builds the component graph from its `config` ROM |

Building the boot image (documented flow, §Building the boot image):

```
git clone https://github.com/genodelabs/genode.git && cd genode
git checkout -b sculpt-25.04 sculpt-25.04
./tool/depot/download genodelabs/bin/x86_64/base-nova/2025-04-29
./tool/create_builddir x86_64
./tool/ports/prepare_port grub2
make -C build/x86_64 run/sculpt KERNEL=nova BOARD=pc DEPOT=omit
# -> build/x86_64/var/run/sculpt.img
```

The boot image is not the installed system. It carries the drivers and the
management UI; the rest of the system is fetched from the depot at runtime.

## 3. Runtime architecture: init as a sandbox

`repos/os/src/init/main.cc` (Genode 26.08) is only ~110 lines because the
component-graph logic lives in `Sandbox` (`repos/os/include/sandbox/sandbox.h`,
`repos/os/src/lib/sandbox/`). Its whole job is:

```cpp
_config.update();
_sandbox.apply_config(_config.node());
```

Consequences that define Sculpt's behaviour:

* **Children are created, updated and destroyed on reconfiguration**
  (`repos/os/src/lib/sandbox/child.cc`, `apply_config()`): a `<start>` node
  that appears starts a child, a node that disappears destroys it. This is why
  Sculpt can install software without rebooting.
* **Quotas are explicit** per child (`caps`, `ram`), with `default` fallbacks.
* **Routing is explicit.** `<route><service name="X"><child name="y"/></service>
  <any-service><parent/></any-service></route>`. A child without a matching
  route simply does not get the session: `[init] Error: <child>: <service>
  environment session denied` (`repos/base/include/base/child.h:596`).
* **Nested init.** A child can itself be `init`, giving a whole sub-tree with
  its own config and its own routing (`+ start drivers | binary init`). Sculpt
  uses this for the driver subsystem.
* **State is observable.** init can emit a `<state>` report (children, RAM,
  caps, sessions) that a GUI can render.

### The config/document pipeline (the heart of Sculpt)

Sculpt's static init config (`repos/gems/run/sculpt.run:194`) contains:

```
+ start report_rom
  + config
    + policy | label: runtime -> config   | report: deploy -> runtime
    + policy | label: runtime -> blueprint | report: runtime -> blueprint

+ start deploy | binary depot_deploy
  + route
    + service ROM | label: config | + child model_rom | label: runtime
    + service ROM | label: blueprint | + child report_rom

+ start runtime | binary monitor | caps: 50000 | ram: 32G | managing_system: yes
  + route
    + service ROM | label: config | + child report_rom
```

So the chain is:

```
/config (file on the "config" file system)
   → model_rom (fs_rom)              exposes files as ROM
   → deploy (depot_deploy)           reads the config, resolves depot packages,
                                     emits a "blueprint" report
   → report_rom                      converts the report into a ROM
   → runtime (monitor ≈ init)        applies the blueprint at runtime
```

`runtime` is not a special OS layer: it is Genode's init with the sandbox
model, given a huge capability/RAM budget and `managing_system: yes`.

**This is the single most important lesson for HermOS.** Sculpt does not have a
"system manager" with privileges; it has a text document, a resolver, and an
init that obeys the document. The GUI (Leitzentrale) is just another client
that edits that document.

## 4. The Leitzentrale (management UI)

The management UI is a set of ordinary components (`sculpt_manager`, a window
manager, a decorator, a terminal) that:

* render the live component graph from init's `<state>` report,
* write the runtime config (`/config`) on behalf of the user,
* show the log (`log_view`) and let the user download packages.

It has no privileged channel: it edits files and casts no special capability.
That is why Sculpt can be scripted from a shell on the same config file.

## 5. Depot, packages, integrity and update

* Everything installable is a **depot archive** (`genodelabs/pkg/...`,
  `genodelabs/src/...`) identified by a version and a content hash.
* A package recipe declares its dependencies; `depot_deploy` resolves the
  transitive closure and emits the concrete start nodes.
* `sculpt.run` itself is an ingredients file (`repos/gems/sculpt/default-pc.sculpt`)
  that lists platform, GPU driver choice, depot providers, option-menu entries
  and preconfigured **presets** (e.g. `window_manager`, `falkon_web_browser`).
* Installed software and the system itself live in **generations** on the
  `GENODE` partition; Sculpt supports on-target system update and switching
  back to a previous generation (documentation §On-target system update).
* The boot image partition layout is GPT with `BIOSBOOT`, `GRUB2` and `GENODE`
  partitions — the same layout HermOS produces with Genode's `image/disk`.

HermOS does **not** have this layer yet. Its components are baked into the boot
image (`build_boot_image`), so "installing" a component means rebuilding.

## 6. Device access and sandboxing

* A nested `drivers` init owns the device drivers.
* `platform` (from `pci_decode` + ACPI reports) hands out `IO_MEM`, `IO_PORT`,
  `IRQ` and `Dma` per PCI device via explicit policies, e.g.
  `+ policy | label_prefix: usb | + pci | class: USB`.
* The IOMMU (VT-d) is used to confine DMA of drivers when available.
* USB devices can be assigned to individual components (documentation
  §Assignment of USB devices to components).
* GUI-level isolation is done by nitpicker **domains**, not by trust.

HermOS already uses this structure: `repos/hermos/run/hermos_desktop.run`
defines a `drivers` init with `acpi`, `pci_decode`, `platform`, `usb`,
`usb_hid`, `ahci`, `nvme`, `ps2` and per-device policies.

## 7. What Sculpt deliberately does not do

* No monolithic "system service" with ambient authority.
* No POSIX-root model; there is no uid 0.
* No implicit inter-component communication: every session is routed.
* No hidden autostart: if it runs, a config says so.

---

## 8. Comparison with HermOS as built (V0.16)

| Concern | SculptOS | HermOS V0.16 | Verdict |
| --- | --- | --- | --- |
| Base | Genode 25.x + NOVA | Genode 26.08 + NOVA | same lineage |
| Boot chain | GRUB2 → bender → NOVA → core → init | same, plus local `image/hermos_iso` UEFI support | adopted |
| System definition | runtime config document | static init config **+ director-owned sandbox config** | partially adopted |
| Runtime composition | yes (deploy → blueprint → init) | yes for workers (director → report_rom → nested init) | **new in V0.16** |
| Component authority | capability routing + quotas | same | adopted |
| Device drivers | nested `drivers` init | nested `drivers` init | adopted |
| Package management | depot archives, generations | none (components baked into boot image) | gap |
| System update/rollback | generations on `GENODE` partition | none | gap |
| Management UI | Leitzentrale (graph, log, files) | HermOS shell (intent UI, worker panel) | different purpose |
| AI/agent layer | none | Hermes Core Director | HermOS-specific |
| Installer | install to disk, update in place | GUI-less executor + host-side image writer | gap |

### What HermOS should take from Sculpt, in order

1. **Runtime composition** — done in V0.16 for workers; next, extend it to
   applications and to the driver reconfiguration path.
2. **A system state report** — init's `<state>` report is the honest source of
   truth for "what is running". HermOS currently derives liveness from Hermes
   sessions only.
3. **Depot packages with content hashes** — needed before any "install an app"
   story is truthful.
4. **Generations + rollback** — required by HermOS postulate 11
   ("self-maintenance must be recoverable").
5. **A text-editable system config** — an escape hatch that keeps the system
   debuggable when the GUI is not running.

### What HermOS should *not* copy

* Sculpt's assumption that the user is a system builder. HermOS's premise is
  that the user expresses intent; the director owns the graph.
* Unrestricted runtime config editing by arbitrary clients. In HermOS the
  config is written by one component (the director) from an allowlist, so a
  client cannot ask for an arbitrary binary.

---

## 9. V0.16 implementation: director-owned worker sandbox

Derived directly from §3: the director writes a config, `report_rom` turns the
report into a ROM, and a nested `init` applies it at runtime.

```
hermes_director_client ──Hermes session──► hermes_director
                                              │ Report "workers"
                                              ▼
                                          report_rom  (policy: workers -> config)
                                              │ ROM "config"
                                              ▼
                                          workers (binary: init)
                                              ├── w1 (binary: hermes_worker)
                                              └── ...  (max 4)
```

* `hermes_director` owns a `Workers` set and an allowlist
  (`{"hermes_worker", caps 160, ram 6M}`). A request supplies only a *name*;
  the binary, caps and RAM always come from the allowlist.
* `Director::_emit_config()` generates the sandbox config with
  `Genode::Xml_generator` and publishes it as the `workers` report.
* `report_rom` policy `label: workers -> config | report: hermes_director -> workers`
  turns that report into the nested init's `config` ROM.
* A worker gets exactly `{LOG, Timer, Hermes}` plus its own binary ROM. It has
  no storage, no device access and no ability to spawn anything.
* Liveness is real, not declared: the director counts worker sessions that
  actually opened (`Hermes worker session ACTIVE label=workers -> w1 -> worker`)
  and decrements when init tears the child down.
* The shell's "Active Agents" counter is fed from that count
  (`response.workers`), so the UI cannot claim a worker that does not exist.

Lifecycle ABI (v2): `WORKER_START`, `WORKER_STOP`, `WORKER_STATUS` on top of
`INTENT`; `Request` gained `arg[24]`, `Response` gained `workers`.

## 10. Pitfalls found while implementing (worth remembering)

* Genode 26.08 init validates its config against `init.hsd`; `binary` is a
  **child node** (`<binary name="x"/>`), not an attribute. The run-script
  generator spells it `+ binary init`, never `| binary: init`.
* A `<service name="X"/>` route entry with no `<child/>`/`<parent/>` inside is
  an *empty* route: the child then fails with
  `X environment session denied`. The target must be explicit.
* The Timer client library is **`timeout`**, not `timer`; `LIBS = timer` fails
  with `Library-description file 'timer.mk' is missing` and the target is
  silently skipped.
* A UEFI ISO needs `serial_fallback` on bender's command line, otherwise a
  headless QEMU boot has no serial console at all.
* `report_rom` report labels are `<client label> -> <report name>`; ROM request
  labels are `<child name> -> <ROM name>`. Both sides must be spelled exactly.

## 11. Evidence

* Build: `artifacts/logs/HermOS_V0.16-testiso-build.log`,
  `artifacts/logs/HermOS_V0.16-release-build.log`
* Runtime (QEMU, OVMF, NOVA): `artifacts/visual-regression/HermOS_V0.16-worker-test.serial.log`
* Screen: `artifacts/visual-regression/HermOS_V0.16-iso-boot.png`
