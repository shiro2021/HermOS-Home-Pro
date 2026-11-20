# HermOS desktop scene (V5 Beta)

`desktop_builder` now draws the whole "Shiro" desktop with an immediate-mode
scene instead of the old per-widget painters.

| File | Role |
|---|---|
| `src/app/desktop_builder/scene_gfx.h` | Freestanding anti-aliased renderer: signed-distance shapes, strokes, glow, bilinear images, text via `stb_truetype` with an arena allocator. No libc, no Genode dependency. |
| `src/app/desktop_builder/scene.h` | Layout, palette, icons, header, search field, navigation rail, Agents & Workers panel, taskbar, left dock, and the Plasma-style panel model. |
| `src/app/desktop_builder/scene_windows.h` | The six built-in windows and their control buttons. |
| `tools/scene_preview/` | Host build of the same code that renders a PPM, for diffing against the design sheet without booting. |

## Why the text used to be scrambled

`font_renderer.h` defined `STBTT_malloc` as `nullptr`. `stb_truetype` needs it
for glyph outlines, so glyph bitmaps were never written and the stack garbage
in the buffer was blitted to the screen. The allocator is now a small arena
that is reset per glyph, shared by the legacy renderer and the scene.

## Why there were two search bars

The old `assets/wallpaper.png` was a damaged crop of a mock-up: it had a
sidebar, a search bar and a mirrored copy of itself baked into the image. The
wallpaper is now clean scenery plus a water reflection, and the scene draws a
single search field.

## Geometry and theme

* The layout is authored in a 1536x1024 design space and mapped with one
  uniform scale (`Scene::Map`), so it is resolution independent.
* `theme.hid` stores window and sidebar geometry in 1/10000 of the screen
  (`scale: 10000`); `theme_engine` converts it to pixels and publishes
  `theme_state`. `desktop_builder` uses the slot rectangles exactly; the fixed
  3x2 grid (`_compute_grid`) is only used by the legacy renderer.
* The theme also carries the panel settings:

  ```
  + panels
    + left
      mode: autohide      # visible | autohide | hidden
      size: 78
    + bottom
      mode: visible
      size: 28
  ```

## Hideable panels (Plasma model)

`Scene::Panel` has an edge, a thickness, a visibility mode and a reveal
animation (ease-out-cubic, ~240 ms, advanced by elapsed time rather than
frame count). `Scene::client_area()` subtracts the struts of panels that are
permanently shown, and is what *maximise* uses.

| Action | Effect |
|---|---|
| Pointer to the left edge (grip tab) | reveals the left launcher dock (auto-hide) |
| `Meta+L` | show / hide the left dock permanently |
| `Meta+B` | show / hide the bottom bar |
| Click a hidden panel's grip tab | show it |
| Click `HermOS` in the bottom bar | restore minimised windows |

The panel layer is drawn separately from the cached base frame, so animating
a panel does not re-render the desktop.

## Host preview

```sh
D=repos/hermos/src/app/desktop_builder
g++ -O2 -std=c++17 -DHERMOS_HOST -I$D -I$D/vendor/stb \
    repos/hermos/tools/scene_preview/scene_preview.cc -o scene_preview
./scene_preview <assets-dir> <fonts-dir> out.ppm [w h] [left-dock 0|1] [bottom-bar 0|1]
```

## Status and limits

* Verified: builds on Genode 26.08 / NOVA, boots under QEMU + OVMF at
  1536x1024, renders the layout, search typing, hover reveal, `Meta+L` /
  `Meta+B`, window close and restore. Not tested on physical hardware.
* The window contents are static mock-up content (System, Files, Hydroponics,
  Create Worker, Software & Updates); only the clock and search text are live.
* Panel choices are not persisted: the live image has no writable storage.
* This is not Dear ImGui. The scene follows the immediate-mode idea (draw the
  whole frame from state each time) with a native renderer, because ImGui has
  no Genode port in this tree.
* `hermos-system.img` (the installer payload) is a placeholder in the test
  build; the installer cannot install from it.
