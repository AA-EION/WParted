#!/bin/sh
# Copyright (C) 2026 WParted contributors
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# Runs the e2fsprogs tools WParted bundles (built with the broker I/O manager
# by helpers/e2fsprogs/build.sh) against a partition inside an image file,
# served through the broker exactly as WParted does on Windows, and checks
# the results with the system's own, unmodified e2fsprogs:
#
#   - create, label, UUID, check, shrink and grow an ext4 file system;
#   - files survive shrinking and growing (checksums compared);
#   - not a single byte outside the partition changes;
#   - read-only sessions can not be written, other device names are refused.
#
# Usage: test_e2fsprogs_broker.sh HARNESS HELPER_DIR WORK_DIR

set -eu

HARNESS=$1
TOOLS=$2
WORK=$3
mkdir -p "$WORK"
cd "$WORK"

MiB=1048576
OFF=$((16 * MiB))
LEN=$((256 * MiB))
IMG=disk.img

fail() { echo "FAIL: $*" >&2; exit 1; }
run() { "$HARNESS" "$IMG" $OFF $LEN -- "$@"; }
run_ro() { "$HARNESS" --ro "$IMG" $OFF $LEN -- "$@"; }

# Disk image with recognisable data around the partition.
rm -f $IMG
dd if=/dev/urandom of=$IMG bs=$MiB count=16 status=none
dd if=/dev/zero bs=$MiB count=256 status=none >> $IMG
dd if=/dev/urandom bs=$MiB count=16 status=none >> $IMG
outside() {
	{ dd if=$IMG bs=$MiB count=16 status=none; dd if=$IMG bs=$MiB skip=272 status=none; } | sha256sum
}
OUTSIDE=$(outside)
part() { dd if=$IMG of=part.img bs=$MiB skip=16 count=256 status=none; }
check_outside() { [ "$(outside)" = "$OUTSIDE" ] || fail "data outside the partition changed ($1)"; }
fsck_ok() {
	part
	e2fsck -fn part.img > fsck.log 2>&1 || { cat fsck.log; fail "e2fsck reports problems ($1)"; }
}

# Files to put on the file system
rm -rf files && mkdir -p files/dir/sub
dd if=/dev/urandom of=files/big.bin bs=$MiB count=40 status=none
printf 'WParted broker test\n' > files/dir/hello.txt
dd if=/dev/urandom of=files/dir/sub/small.bin bs=4096 count=3 status=none
(cd files && find . -type f -exec sha256sum {} \; | sort -k2) > files.sha256

echo "== mke2fs (GParted's options) with files"
run "$TOOLS/mke2fs" -F -t ext4 -L wparted -d files wparted-broker || fail "mke2fs"
check_outside mke2fs
fsck_ok mke2fs
[ "$(blkid -o value -s LABEL part.img)" = wparted ] || fail "label after mke2fs"

echo "== dumpe2fs read-only"
run_ro "$TOOLS/dumpe2fs" -h wparted-broker > dumpe2fs.txt 2>/dev/null || fail "dumpe2fs"
grep -q "^Filesystem volume name: *wparted" dumpe2fs.txt || fail "dumpe2fs output"

echo "== tune2fs label and UUID"
run "$TOOLS/tune2fs" -L renamed wparted-broker > /dev/null || fail "tune2fs -L"
run "$TOOLS/tune2fs" -U 01234567-89ab-cdef-0123-456789abcdef wparted-broker > /dev/null 2>&1 || fail "tune2fs -U"
fsck_ok tune2fs
[ "$(blkid -o value -s LABEL part.img)" = renamed ] || fail "label after tune2fs"
[ "$(blkid -o value -s UUID part.img)" = 01234567-89ab-cdef-0123-456789abcdef ] || fail "uuid after tune2fs"
check_outside tune2fs

echo "== e2fsck as GParted runs it"
run "$TOOLS/e2fsck" -f -y -v -C 0 wparted-broker > e2fsck.log 2>&1 || fail "e2fsck"
check_outside e2fsck

echo "== resize2fs shrink to 128M"
run "$TOOLS/e2fsck" -f -y wparted-broker > /dev/null 2>&1 || fail "e2fsck before shrink"
run "$TOOLS/resize2fs" -p wparted-broker 131072K > resize.log 2>&1 || { cat resize.log; fail "resize2fs shrink"; }
fsck_ok shrink
[ "$(dumpe2fs -h part.img 2>/dev/null | sed -n 's/^Block count: *//p')" = 32768 ] || fail "size after shrink"
check_outside shrink

echo "== resize2fs grow to the partition"
run "$TOOLS/resize2fs" -p wparted-broker > resize.log 2>&1 || { cat resize.log; fail "resize2fs grow"; }
fsck_ok grow
[ "$(dumpe2fs -h part.img 2>/dev/null | sed -n 's/^Block count: *//p')" = 65536 ] || fail "size after grow"
check_outside grow

echo "== files after shrink and grow"
rm -rf out && mkdir out
debugfs -R "rdump / out" part.img > /dev/null 2>&1 || fail "debugfs rdump"
(cd out && find . -type f ! -path './lost+found/*' -exec sha256sum {} \; | sort -k2) > out.sha256
diff files.sha256 out.sha256 || fail "file contents changed"

echo "== read-only session refuses writes"
if run_ro "$TOOLS/tune2fs" -L nope wparted-broker > /dev/null 2>&1; then
	fail "tune2fs wrote through a read-only session"
fi
part
[ "$(blkid -o value -s LABEL part.img)" = renamed ] || fail "label changed through read-only session"

echo "== only the broker device"
if run "$TOOLS/mke2fs" -F -t ext4 "$WORK/$IMG" > /dev/null 2>&1; then
	fail "mke2fs accepted a file name instead of the broker device"
fi
if run "$TOOLS/e2fsck" -n /dev/sda > /dev/null 2>&1; then
	fail "e2fsck accepted /dev/sda"
fi
check_outside "refusals"
fsck_ok final

echo "PASS: e2fsprogs through the broker"
