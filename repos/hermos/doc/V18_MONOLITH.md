# HermOS v18 — monolithic desktop (Phase 1)

Status: first milestone built and boot-verified. See
`artifacts/visual-regression/HermOS_V0.18-monolith.png` and
`artifacts/logs/HermOS_V0.18-monolith-build.log`.

## What is implemented

* `src/app/desktop_builder` — owns the Framebuffer session and paints the
  desktop itself (hub + 8 hexagonal Cells + connectors + glass panels) using
  the one canonical geometry from `hermos_ui/window_geometry.h`.
* `src/driver/framebuffer/hermos_fb` — a minimal framebuffer driver that reads
  the UEFI GOP framebuffer from `platform_info`, maps it with IO_MEM and hands
  the dataspace straight to its client. Direct scanout: no copy, no Capture.
* `run/hermos_monolith.run` — a composition with **no Nitpicker and no Capture**:
  `hermos_fb → desktop_builder`.

Boot log (QEMU/OVMF/NOVA):

```
[init -> hermos_fb]     hermos_fb: direct scanout 1024x768 pitch=4096 (no capture, no nitpicker)
[init -> desktop_builder] HermOS desktop_builder owns framebuffer 1024x768 (no nitpicker in this composition)
[init -> desktop_builder] HermOS desktop_builder rendered 9 hexagonal Cells directly to the framebuffer
```

## Blueprint corrections found while building (grounded in the 26.08 tree)

1. **`boot_fb` needs Capture.** In 26.08 the stock `driver/framebuffer/boot`
   unconditionally opens a `Capture` session at startup (its framebuffer is a
   capture surface). Capture is served by Nitpicker, so a true "no nitpicker"
   desktop needs a direct-scanout driver — hence `hermos_fb` above.
2. **Kernel is NOVA, not base-hw.** Everything built so far targets NOVA
   (`KERNEL=nova`). `base-hw` is a different kernel; the blueprint's
   "Base-hw" is a separate (later) port, not a config flag.
3. **Goa is not the system-image build tool.** Goa builds *applications* for an
   existing Sculpt-style system. A bootable HermOS image is assembled by the
   classic `build/` + `*.run` path used throughout this project. (Goa remains
   the right tool for third-party app Cells in Phase 3.)
4. **The skeleton's `Gui::Session` stub is far too thin.** The server side of
   `Gui::Session` is essentially Nitpicker's `gui_session.h` — framebuffer
   dataspace, input event queue, view geometry, buffer/focus/alpha handling.
   Hosting apps inside Cells means implementing that. This is the next
   milestone, not this one.
5. **HID config is available but optional.** 26.08 ships `init.hsd` and
   `tool/hid`; our run scripts use the classic generator that emits XML. Both
   are accepted — switching to HID is cosmetic, independent of the monolith.

## Next (Phase 1b)

* Server-side `Gui::Session` in `desktop_builder` so an application can connect
  and render inside a Cell.
* Input: `Event` session from the driver stack (the existing `event_to_input`
  distributor already fans events out).
* Hit-testing: `Window_geometry::contains` already implements the hexagon
  point-in-shape test.
