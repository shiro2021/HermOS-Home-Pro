#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'EOF'
Install a HermOS hybrid ISO or Genode GPT/EFI disk image to a whole disk.

Usage:
  sudo install-hermos-to-disk.sh IMAGE.iso|IMAGE.img /dev/disk

This replaces the target disk's partition table and data. It writes the
bootable HermOS image directly, following Sculpt OS's documented image-write
installation method. It writes the image's fixed partition layout and does not
resize or create a persistent data partition.
EOF
}

if [[ $# -eq 1 && $1 == --help ]]; then usage; exit 0; fi
if [[ $# -ne 2 ]]; then usage >&2; exit 2; fi
image=$1
target=$(readlink -f -- "$2")

if [[ $EUID -ne 0 ]]; then
	echo "Run this installer as root (for example, with sudo)." >&2
	exit 1
fi
if [[ ! -f $image || ! -r $image ]]; then
	echo "Cannot read image: $image" >&2
	exit 1
fi
if [[ ! -b $target ]]; then
	echo "Target must be a whole block device: $target" >&2
	exit 1
fi
if [[ $(lsblk -dnro TYPE -- "$target") != disk ]]; then
	echo "Select a whole disk, not a partition: $target" >&2
	exit 1
fi

image_bytes=$(stat -c %s -- "$image")
disk_bytes=$(blockdev --getsize64 "$target")
if (( disk_bytes < image_bytes )); then
	echo "Target is smaller than the image ($disk_bytes < $image_bytes bytes)." >&2
	exit 1
fi

# Never permit the currently booted system disk as the target.
root_source=$(findmnt -nro SOURCE --target /)
if [[ -b $root_source ]]; then
	root_disk=$(lsblk -snro PATH -- "$root_source" | tail -n 1)
	if [[ $root_disk == "$target" ]]; then
		echo "Refusing to overwrite the disk containing the running system: $target" >&2
		exit 1
	fi
fi

# A mounted child partition means the target is in use. Do not unmount it
# implicitly, because that could interrupt another application or user.
while IFS= read -r mountpoint; do
	if [[ -n $mountpoint ]]; then
		echo "Refusing target with a mounted filesystem: $mountpoint" >&2
		exit 1
	fi
done < <(lsblk -nrpo MOUNTPOINT -- "$target" | sed '/^[[:space:]]*$/d')

model=$(lsblk -dnro MODEL -- "$target" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
size=$(lsblk -dnro SIZE -- "$target")
image_hash=$(sha256sum -- "$image" | awk '{print $1}')

cat <<EOF
HermOS disk installation

Image:  $image
SHA256: $image_hash
Target: $target
Model:  ${model:-unknown}
Size:   $size

All existing data and partitions on $target will be replaced.
EOF
read -r -p "To continue, type the target path exactly ($target): " answer
if [[ $answer != "$target" ]]; then
	echo "Target confirmation did not match; no data was written."
	exit 1
fi

dd if="$image" of="$target" bs=4M iflag=fullblock conv=fsync status=progress
blockdev --flushbufs "$target"

# Verify the entire written image, not just the ISO header.
readback_hash=$(head -c "$image_bytes" -- "$target" | sha256sum | awk '{print $1}')
if [[ $readback_hash != "$image_hash" ]]; then
	echo "Verification failed: expected $image_hash, read back $readback_hash." >&2
	exit 1
fi

echo "HermOS image written and verified on $target. Power off before removing the disk."
