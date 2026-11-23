#!/usr/bin/env bash
# Collect what the HermOS Browser needs into ONE tar archive (browser.tar) in the output directory:
# the program (built by Goa) and exactly the libraries it needs from the verified depot, found by following
# each library's NEEDED entries, plus the few things the program loads by name at run time.
# The browser gets the depot's own libc, libm, stdcxx, vfs plugins, zlib and openssl too: the Qt and Chromium
# libraries were built against exactly those, and mixing them with another libc build crashed at run time.
# The dynamic linker too: the depot's libc needs symbols of the base library's own release (HermOS uses a newer
# one, with a different Timeout constructor), and the linker carries the base library.
# Usage: assemble-modules.sh PROJECT_DIR OUT_DIR
set -euo pipefail
P=${1:?goa project dir}; OUT=${2:?output dir}
READELF=${READELF:-/usr/local/genode/tool/current/bin/genode-x86-readelf}
python3 - "$P" "$OUT" "$READELF" <<'PY'
import glob, os, shutil, subprocess, sys, tarfile
P, OUT, READELF = sys.argv[1:4]
D = f"{P}/var/depot/cproc"
own = set()                                      # everything, the dynamic linker included, matches the depot build
avail = {}                                       # file name -> path, from the verified depot archives
for f in glob.glob(f"{D}/bin/x86_64/*/*/*") + glob.glob(f"{D}/raw/*/*/*") + glob.glob(f"{P}/var/depot/genodelabs/bin/x86_64/base-nova/*/ld.lib.so"):
    if os.path.isfile(f) and os.path.basename(f) not in ("LICENSE",): avail[os.path.basename(f)] = f
avail["hermos_browser"] = f"{P}/var/build/x86_64/hermos_browser"

def needed(path):
    out = subprocess.run([READELF, "-d", path], capture_output=True, text=True).stdout
    return [l.split("[")[1].split("]")[0] for l in out.splitlines() if "NEEDED" in l]

# loaded by name at run time (plugins, data, virtual file system plugins, NSS modules, the software GL driver)
dynamic = ["ld.lib.so", "vfs_pipe.lib.so", "vfs_lwip.lib.so", "vfs_jitterentropy.lib.so", "vfs_xoroshiro.lib.so", "vfs_qtwebengine_shm.lib.so", "vfs_gpu.lib.so", "mesa_gpu-softpipe.lib.so",
           "nssckbi.lib.so", "softokn3.lib.so", "freebl3.lib.so", "libqgenode.lib.so",
           "qt6_dejavusans.tar", "qt6_libqgenode.tar", "qt6_libqjpeg.tar", "qt6_libqsqlite.tar",
           "qt6_webengine_resources.tar", "qt6_webengine_translations.tar"]
todo, chosen, missing = ["hermos_browser"] + dynamic, {}, set()
while todo:
    n = todo.pop()
    if n in chosen or n in own: continue
    if n not in avail: missing.add(n); continue
    chosen[n] = avail[n]
    if n.endswith(".lib.so") or n == "hermos_browser":
        todo += needed(avail[n])
if missing: sys.exit("ERROR: not in the depot set: " + " ".join(sorted(missing)))
shutil.rmtree(OUT, ignore_errors=True); os.makedirs(OUT)
with tarfile.open(f"{OUT}/browser.tar", "w", format=tarfile.USTAR_FORMAT) as t:
    for n in sorted(chosen): t.add(chosen[n], arcname=n)
size = sum(os.path.getsize(p) for p in chosen.values()) >> 20
print(f"browser.tar: {len(chosen)} files, {size} MB")
PY
