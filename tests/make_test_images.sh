#!/bin/sh
# Copyright (C) 2026 WParted contributors
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# Create disk and file system images with the real Linux tools (sfdisk,
# sgdisk, mkfs.*) so that the WParted partition table parser and file system
# prober are tested against independent implementations, plus the blkid
# results the prober must reproduce.
#
# Usage: make_test_images.sh OUTPUT_DIRECTORY
# Then:  WPARTED_TEST_IMAGES=OUTPUT_DIRECTORY ./wparted_unit_tests

set -eu

OUT=${1:?usage: make_test_images.sh OUTPUT_DIRECTORY}
mkdir -p "$OUT"
cd "$OUT"

# --- Partition tables -----------------------------------------------------

rm -f mbr.img gpt.img
truncate -s 64M mbr.img
sfdisk --quiet mbr.img <<'EOF'
label: dos
label-id: 0x0a0b0c0d
unit: sectors

start=2048, size=20480, type=7, bootable
start=22528, size=106496, type=5
start=24576, size=20480, type=83
start=47104, size=20480, type=82
start=69632, size=20480, type=c
EOF

truncate -s 64M gpt.img
sgdisk --clear --disk-guid=11111111-2222-3333-4444-555555555555 \
	--new=1:2048:+2M   --typecode=1:EF00 --change-name=1:"EFI system partition" \
	--new=2:0:+1M      --typecode=2:0C01 \
	--new=3:0:+16M     --typecode=3:0700 --change-name=3:"Datos é" \
	                   --attributes=3:set:62 --attributes=3:set:63 \
	--new=6:0:+8M      --typecode=6:2700 --attributes=6:set:0 \
	gpt.img > /dev/null

# --- File systems ---------------------------------------------------------

: > expected.txt
mk() {
	name=$1; size=$2; shift 2
	rm -f "$name"
	truncate -s "$size" "$name"
	"$@" "$name" > /dev/null 2>&1
	# blkid low level probe; keep TYPE SEC_TYPE UUID LABEL in fixed order.
	eval "$(blkid -p -o export "$name" | grep -E '^(TYPE|SEC_TYPE|UUID|LABEL)=' | sed 's/^/BLKID_/')"
	printf '%s|%s|%s|%s|%s\n' "$name" "${BLKID_TYPE:-}" "${BLKID_SEC_TYPE:-}" "${BLKID_UUID:-}" \
		"${BLKID_LABEL:-}" >> expected.txt
	unset BLKID_TYPE BLKID_SEC_TYPE BLKID_UUID BLKID_LABEL
}

mk ext2.img   16M mkfs.ext2 -q -F -L e2label -U 0e2e2e2e-1111-2222-3333-444444444444
mk ext3.img   16M mkfs.ext3 -q -F -L e3label
mk ext4.img   32M mkfs.ext4 -q -F -L e4label
mk fat16.img  32M mkfs.vfat -F 16 -n FAT16LBL -i 1234ABCD
mk fat32.img  64M mkfs.vfat -F 32 -n FAT32LBL -i DEADBEEF
mk ntfs.img   32M mkfs.ntfs -q -F -f -L NTFSLabel
mk exfat.img  32M mkfs.exfat -L ExFatLbl
mk btrfs.img 128M mkfs.btrfs -q -f -L btrlabel
mk xfs.img   320M mkfs.xfs -q -f -L xfslabel
mk swap.img   16M mkswap -L swaplabel
mk f2fs.img   64M mkfs.f2fs -q -f
if command -v cryptsetup > /dev/null 2>&1; then
	rm -f luks.img
	truncate -s 32M luks.img
	printf 'test-passphrase' | cryptsetup luksFormat --batch-mode --type luks2 \
		--pbkdf pbkdf2 --pbkdf-force-iterations 1000 --label lukslabel luks.img - > /dev/null 2>&1
	eval "$(blkid -p -o export luks.img | grep -E '^(TYPE|SEC_TYPE|UUID|LABEL)=' | sed 's/^/BLKID_/')"
	printf '%s|%s|%s|%s|%s\n' luks.img "${BLKID_TYPE:-}" "${BLKID_SEC_TYPE:-}" "${BLKID_UUID:-}" \
		"${BLKID_LABEL:-}" >> expected.txt
fi

echo "Test images created in $OUT"
cat expected.txt
