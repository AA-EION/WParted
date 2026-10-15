#!/usr/bin/env bash
# Build the e2fsprogs tools WParted bundles (mke2fs, e2fsck, resize2fs,
# tune2fs, dumpe2fs) with the broker I/O manager, so that they can only reach
# the partition WParted serves them.
#
# Usage: helpers/e2fsprogs/build.sh OUTPUT_DIR [configure arguments...]
#   In an MSYS2 UCRT64 shell this builds the Windows helpers.  On Linux it
#   builds the same tools natively, which the unit tests use to check the
#   broker I/O manager (tests/test_E2fsprogsBroker.sh).
#   Cross builds: pass --host=x86_64-w64-mingw32.

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
mkdir -p "$1"
OUT=$(cd "$1" && pwd)
shift

read -r name version sha url < <(grep -E '^e2fsprogs[[:space:]]' "$ROOT/third_party/sources.lock")
WORK="$OUT/work"
TARBALL="$WORK/e2fsprogs-$version.tar.xz"
mkdir -p "$WORK"

if [[ ! -f "$TARBALL" ]] || ! echo "$sha  $TARBALL" | sha256sum -c --quiet - 2>/dev/null; then
	echo "==> Downloading e2fsprogs $version"
	curl -fsSL --retry 3 -o "$TARBALL.part" "$url"
	mv "$TARBALL.part" "$TARBALL"
fi
echo "$sha  $TARBALL" | sha256sum -c --quiet - || { echo "error: e2fsprogs tarball checksum mismatch" >&2; exit 1; }

SRC="$WORK/src"
BUILD="$WORK/build"
rm -rf "$SRC" "$BUILD"
mkdir -p "$SRC" "$BUILD"
tar xf "$TARBALL" -C "$SRC" --strip-components=1
for p in "$HERE"/patches/*.patch; do
	echo "==> Applying $(basename "$p")"
	patch -p1 -d "$SRC" < "$p" > /dev/null
done
cp "$HERE/wparted_io.c" "$SRC/lib/ext2fs/wparted_io.c"

windows=no
case "$* $(uname -s)" in
	*mingw*|*MINGW*|*MSYS*) windows=yes ;;
esac

CPPFLAGS="-DWPARTED_BROKER_IO -include $HERE/wparted_e2fs.h -I$ROOT/helpers/broker"
LDFLAGS=""
if [[ $windows == yes ]]; then
	CPPFLAGS="$CPPFLAGS -I$HERE/compat"
	LDFLAGS="-static"
fi

CC_TOOL=${CC:-cc}
for a in "$@"; do
	case "$a" in --host=*) CC_TOOL="${a#--host=}-gcc" ;; esac
done
"$CC_TOOL" -O2 -Wall -c "$ROOT/helpers/broker/wpbroker.c" -I"$ROOT/helpers/broker" -o "$BUILD/wpbroker.o"

echo "==> Configuring"
(cd "$BUILD" && "$SRC/configure" "$@" \
	--disable-nls --disable-fuse2fs --disable-defrag --disable-uuidd --disable-debugfs \
	--disable-imager --disable-e2initrd-helper --disable-tls --disable-fsck \
	--disable-elf-shlibs --disable-bsd-shlibs --enable-libblkid --enable-libuuid \
	--without-libarchive --without-pthread \
	CPPFLAGS="$CPPFLAGS" LDFLAGS="$LDFLAGS" LIBS="$BUILD/wpbroker.o" > configure.log 2>&1) || {
	tail -30 "$BUILD/configure.log" >&2; exit 1; }

# Build the broker I/O manager in place of unix_io / windows_io.
sed -i -E 's/\b(unix_io|windows_io)\.(o|c)\b/wparted_io.\2/g' "$BUILD/lib/ext2fs/Makefile"

echo "==> Building libraries"
for d in lib/et lib/uuid lib/blkid lib/ext2fs lib/e2p lib/support; do
	make -C "$BUILD/$d" -j"$(nproc)" > "$BUILD/make-$(basename "$d").log" 2>&1 || {
		tail -30 "$BUILD/make-$(basename "$d").log" >&2; exit 1; }
done

echo "==> Building tools"
build_tool()
{
	local dir=$1 target=$2
	make -C "$BUILD/$dir" -j"$(nproc)" "$target" > "$BUILD/make-$target.log" 2>&1 || {
		grep -E "error|undefined reference" "$BUILD/make-$target.log" | head -40 >&2
		tail -15 "$BUILD/make-$target.log" >&2; exit 1; }
}
build_tool misc mke2fs
build_tool misc tune2fs
build_tool misc dumpe2fs
build_tool e2fsck e2fsck
build_tool resize resize2fs

ext=""
[[ $windows == yes ]] && ext=".exe"
for t in misc/mke2fs misc/tune2fs misc/dumpe2fs e2fsck/e2fsck resize/resize2fs; do
	cp "$BUILD/$t$ext" "$OUT/"
done
echo "==> e2fsprogs $version helpers in $OUT"
