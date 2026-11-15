# HermOS visual system

## Canonical inputs and confidence

The canonical files and their availability are listed in [`design/reference-inventory.md`](design/reference-inventory.md). The desktop concept and 9-screen installer storyboard are composite reference pictures. Their silhouette, panel placement, colors, typography hierarchy, background direction, and state semantics are visible facts. Pixel-level measurements need a defined viewport/crop and should be measured against the unchanged originals before claiming a match.

The supplied PNG emblem is available and is the canonical glyph. Preserve it byte-for-byte. The SVG is explicitly titled a color-quantized vector trace, so it is a derivative, not proof of exact vector geometry. `A MORE HUMAN COMPUTER` is the approved brand line. The wallpaper in a composite is not a standalone source image; any crop must be declared as derived.

## Locked geometry and tokens

The shared window outline is exactly six vertices, clockwise:

```text
(C,0), (W-C,0), (W,H/2), (W-C,H), (C,H), (0,H/2)
```

The title bar, frame, content mask and active/inactive border use the same polygon. Avoid adding sub-corners to the outline. V7 fixes the geometry but does not provide a measured numeric corner inset or frame thickness; choose those from image measurements and record them before pixel matching.

| Token | Initial visual rule | State |
| --- | --- | --- |
| Graphite/carbon | Dark, subtly textured material | Reference-observed; exact color values not yet measured |
| Solar Orange | Restrained active/focus accent and soft edge glow | Named and reference-observed; exact values not yet measured |
| Inactive gray | Low-emphasis frame/control/state | Reference-observed; exact values not yet measured |
| Dawn landscape | Mountain/lake, mist, blue/gold light, calm contrast | Reference-observed; clean source wallpaper not separately supplied |
| Typography | High-contrast light text, clear heading/body/status hierarchy | Reference-observed; exact family/sizes not measured |

## Shared implementation requirements

One shared window primitive owns the polygon, carbon title material, title placement, top-right minimize/maximize/close controls, frame state and content mask. Shared buttons, input, sidebar, worker rows, details and subpanels must express enabled/disabled, active/inactive, waiting, failed and approval states. A control shown as active must work; an unsupported control must look disabled and be documented.

The right-side Agents & Workers sidebar follows the desktop reference's right edge, dark surface, narrow icon rail, active Solar Orange rows, restrained inactive rows, and nested detail surface. Values shown there must be read from Hermes. A static approximation is not a live panel.

## User-provided desktop screenshot

The screenshot is the visual source for the desktop home surface. At 1600×900 it shows: a thin translucent header with HermOS branding and system indicators; a six-entry glass navigation rail on the left; a central Hermes emblem inside a Solar Orange hex, linked to seven labeled hex work areas; a right-side Active Agents panel above a Hermes prompt panel; and separate wallpaper, app, and system shelves along the bottom. The screenshot uses blue glass and a cinematic dawn lake scene. The older desktop concept remains a distinct reference with carbon panels and Solar Orange frames; it must not be silently conflated with this newer screenshot.

The implementation uses the screenshot's live composition and a separate landscape asset. The 1024×768 QEMU display is 4:3 while the supplied screenshot is 16:9, so the background is center-cropped and the relative proportions are estimated for the available viewport. `HermOS-Dawn-Lake-derived.png` is an ImageGen edit based on the reference screenshot with the UI removed; it is not canonical artwork. Worker state is truthful: the Hermes Director reports READY, while no agent workers are configured, so the live Active Agents count is zero.

## Visual verification checklist

| Reference | Intended implementation | Status |
| --- | --- | --- |
| User desktop screenshot, 1600×900 | Native home composition with header, left rail, central hub, right panels, and bottom shelves | PARTIAL: central hex proportions and dock icon forms are closer, but the 1024×768 viewport, wallpaper details, typography, icons, worker rows, and remaining spacing still differ. Input proof is at `artifacts/visual-regression/hermos-native-ui-input-proof.png`. |
| Earlier desktop concept | Six-sided window system, carbon material, Solar Orange active frames, installer and subpanel patterns | PARTIAL: central six-sided hub and some tokens are present, but the pictured desktop windows, details, and installer are not implemented. |
| Installer storyboard, screens 1–9 | Shared installer shell and real read-only discovery/planning/review/execution states | NOT_IMPLEMENTED |
| Golden Compass emblem | Supplied bitmap embedded in the shell | PARTIAL: exact bitmap is embedded and visible; placement/scale has not been pixel-matched. |
| Keyboard and pointer | USB HID keyboard/tablet through Event-to-Input into the shell | PASS for basic UEFI QEMU interaction: cursor motion, field focus, and keyboard insertion are visible in the input-proof capture. Physical device behavior is untested. |

Exact visual fidelity is not claimed. The comparison image preserves both images at their natural dimensions; it does not stretch the runtime capture to hide the viewport mismatch.

## Captured failure states

The latest UEFI QEMU desktop capture is `artifacts/visual-regression/hermos-desktop-sculpt-ref.png`. Historical boot failures are retained in `artifacts/visual-regression/uefi-grub-no-video-mode.png` and `artifacts/visual-regression/bios-no-framebuffer.ppm`.

For each implemented state, capture the same practical viewport, compare side by side with the original, and save comparisons under `artifacts/visual-regression/`. Exact fidelity must not be claimed before that comparison and interaction checks.
