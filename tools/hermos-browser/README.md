# HermOS Browser

A small, disposable web Cell for people moving over from the old way of using a PC. HermOS itself stays
"type what you want and it delivers"; this is the bridge for the moments a real website is needed.

* **One bar** for addresses and questions (`src/smartbar.h`, tested on the host: `tests/smartbar_test.cpp`).
* **Disposable (REA):** in-memory profile, nothing written, downloads and permission prompts denied.
* **Shield:** HTTPS only (no downgrade), tracker/ad blocklist with a live counter (`src/shield.h`, `src/blocklist.txt`).
* **One page per Cell:** a link that wants a new window loads in place.
* **HermOS look** from a data file (`src/theme.h`, `/hermos-browser.theme`): orange when focused, carbon gray when not.
* **Engine:** Qt WebEngine (Chromium 122, patched to 134) from the signed `cproc` Genode depot; the program is
  built with Goa against the depot's Qt 6.8.3 API (`project/`).
* **Memory:** no number is chosen. The browser's RAM quota is deliberately oversized; init gives it all that is
  left (verified: "configured RAM exceeds available RAM, proceed with 4330291072" on a 6 GB machine).

## Build

```
tools/hermos-browser/fetch-depot.sh project <genode>/repos/gems/sculpt/depot < depot-archives.txt   # verified downloads
(cd project && goa build)                      # needs Goa (github.com/genodelabs/goa) and the Genode toolchain
tools/hermos-browser/assemble-modules.sh project OUT        # OUT/browser.tar: the program + exactly the libraries it needs
tools/hermos-browser/build-test-image.sh OUT IMAGE.img      # a HermOS disk image with the browser (the normal ISO has none)
```

## Status (bring-up)

Works: the program builds and links against Qt6 WebEngine; the smart bar tests pass; the image boots; the
browser's files are served from `browser.tar` by `tar_rom`; the adaptive RAM grant works.

**Blocked: the depot's binaries cannot run on HermOS's Genode.** The Qt/Chromium libraries in the depot were
built against Genode 26.04/26.05; HermOS is on 26.08. With HermOS's own linker the depot's libc fails to resolve
`Genode::Timeout::Timeout(Timer::Connection&)` (the constructor changed). With the depot's matching linker
(`base-nova` 2026-04-16 and 2026-06-04 both tried) the program stops immediately after the linker loads, before
any library is requested: `attempt of IPC call to an invalid object`, i.e. the linker's base interface does not
match HermOS's init/core. Re-routing cannot fix that.

Ways forward: (1) a HermOS edition built on the Genode release the depot targets; (2) depot binaries built for
26.08 (not published yet); (3) building Qt6 WebEngine from source against 26.08 (needs the Chromium sources
and many hours).
