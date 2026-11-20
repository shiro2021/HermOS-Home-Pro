# Building HermOS V5 Beta

Genode 26.08 tree (tag `26.08`, https://codeberg.org/genodelabs/genode) with
the Genode toolchain, and this repo linked as `repos/hermos`.

`build/x86_64/etc/build.conf` must list **all** of these repositories:

```
REPOSITORIES += $(GENODE_DIR)/repos/base-nova
REPOSITORIES += $(GENODE_DIR)/repos/base
REPOSITORIES += $(GENODE_DIR)/repos/os
REPOSITORIES += $(GENODE_DIR)/repos/demo
REPOSITORIES += $(GENODE_DIR)/repos/hermos
REPOSITORIES += $(GENODE_DIR)/repos/libports
REPOSITORIES += $(GENODE_DIR)/repos/ports
REPOSITORIES += $(GENODE_DIR)/repos/gems
REPOSITORIES += $(GENODE_DIR)/repos/pc
REPOSITORIES += $(GENODE_DIR)/repos/dde_linux
REPOSITORIES += $(GENODE_DIR)/repos/dde_ipxe     # NIC driver (ipxe_nic)
```

**`dde_ipxe` is required for networking.** `hermos_monolith.run` starts the
NIC with `binary ipxe_nic`. Without that repository the binary is not built,
the image has no NIC driver, and the log fills with
`nic: environment ROM session denied (label="nic")`.

Ports: `prepare_port curl jitterentropy libc libpng libssh linux lwip openssl
stdcxx zlib bash coreutils stb ttf-bitstream-vera x86emu nova grub2 dde_ipxe`.

Image and ISO:

```sh
make run/hermos_monolith KERNEL=nova BOARD=pc CROSS_DEV_PREFIX=genode-x86- \
     RUN_OPT="--include image/iso --include boot_dir/nova --hermos-build-only yes"
tools/iso/make-hybrid-iso.sh build/x86_64/var/run/hermos_monolith.iso HermOS.iso
```

The desktop needs a **UEFI** boot (`hermos_fb` reads a UEFI framebuffer).

`hermos-system.img` (installer payload) must exist in `build/x86_64/bin/`; a
zero-filled placeholder is enough for a live ISO, but then the installer
cannot install.
