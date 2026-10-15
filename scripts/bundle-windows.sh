#!/usr/bin/env bash
# Bundle WParted and the GTK 3 runtime from MSYS2 UCRT64 into a portable folder.
#
# Run from the source tree in an MSYS2 UCRT64 shell after building:
#   scripts/bundle-windows.sh [BUILD_DIR] [DIST_DIR]
#
# Result (relocatable, runs from any folder):
#   DIST_DIR/bin/wparted.exe       the program plus every DLL it needs
#   DIST_DIR/lib/gdk-pixbuf-2.0    image loaders and their relative loaders.cache
#   DIST_DIR/share/                icons, GSettings schemas, translations
#   DIST_DIR/licenses/             licenses of the bundled runtime

set -euo pipefail

BUILD_DIR=${1:-build}
DIST=${2:-dist/WParted}
PREFIX=${MINGW_PREFIX:-/ucrt64}
PKG_PREFIX=${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}
LOADERS_REL=lib/gdk-pixbuf-2.0/2.10.0/loaders

if [[ ! -x "$PREFIX/bin/gdk-pixbuf-query-loaders.exe" ]]; then
	echo "error: run this in an MSYS2 UCRT64 shell with gtkmm3 installed (MINGW_PREFIX=$PREFIX)" >&2
	exit 1
fi
if [[ ! -f "$BUILD_DIR/wparted.exe" ]]; then
	echo "error: $BUILD_DIR/wparted.exe not found, build first" >&2
	exit 1
fi

echo "==> Installing WParted into $DIST"
rm -rf "$DIST"
cmake --install "$BUILD_DIR" --prefix "$DIST"


# --- Runtime DLLs -------------------------------------------------------------
# Copy the recursive closure of DLL imports that are found in $PREFIX/bin.
# Anything not found there is a Windows system DLL and is not bundled.

declare -A seen=()
queue=()

add_imports()
{
	local file=$1 dll
	while read -r dll; do
		dll=${dll,,}
		[[ -n "${seen[$dll]:-}" ]] && continue
		seen[$dll]=1
		if [[ -f "$PREFIX/bin/$dll" ]]; then
			cp -f "$PREFIX/bin/$dll" "$DIST/bin/"
			queue+=("$PREFIX/bin/$dll")
		fi
	done < <(objdump -p "$file" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r')
}

process_queue()
{
	while ((${#queue[@]})); do
		local next=${queue[0]}
		queue=("${queue[@]:1}")
		add_imports "$next"
	done
}

echo "==> Collecting runtime DLLs"
queue+=("$DIST/bin/wparted.exe")
process_queue

# GLib's process spawning helper, used by gtk_show_uri() fallbacks.
for helper in gspawn-win64-helper.exe gspawn-win64-helper-console.exe; do
	if [[ -f "$PREFIX/bin/$helper" ]]; then
		cp -f "$PREFIX/bin/$helper" "$DIST/bin/"
		queue+=("$DIST/bin/$helper")
	fi
done
process_queue


# --- gdk-pixbuf loaders (PNG, and SVG for the symbolic icons) ------------------

echo "==> Bundling gdk-pixbuf loaders"
mkdir -p "$DIST/$LOADERS_REL"
for loader in "$PREFIX/$LOADERS_REL"/*.dll; do
	cp -f "$loader" "$DIST/$LOADERS_REL/"
	queue+=("$loader")
done
process_queue

# MSYS2 builds gdk-pixbuf relocatable: run from inside the bundle, the query
# tool writes the module paths relative to the bundle root.
cp -f "$PREFIX/bin/gdk-pixbuf-query-loaders.exe" "$DIST/bin/"
(cd "$DIST/bin" && ./gdk-pixbuf-query-loaders.exe --update-cache)
rm -f "$DIST/bin/gdk-pixbuf-query-loaders.exe"
CACHE="$DIST/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
if [[ ! -s "$CACHE" ]] || grep -q -E '^"([A-Za-z]:)?[\\/]' "$CACHE"; then
	echo "error: $CACHE is missing or holds absolute paths, the bundle would not be relocatable" >&2
	exit 1
fi


# --- GSettings schemas (GTK file chooser, colour chooser, ...) -----------------

echo "==> Compiling GSettings schemas"
mkdir -p "$DIST/share/glib-2.0/schemas"
cp -f "$PREFIX"/share/glib-2.0/schemas/org.gtk.*.gschema.xml "$DIST/share/glib-2.0/schemas/"
glib-compile-schemas "$DIST/share/glib-2.0/schemas"


# --- Icon themes ----------------------------------------------------------------

echo "==> Bundling icon themes"
mkdir -p "$DIST/share/icons"
for theme in Adwaita AdwaitaLegacy hicolor; do
	if [[ -d "$PREFIX/share/icons/$theme" ]]; then
		cp -rf "$PREFIX/share/icons/$theme" "$DIST/share/icons/"
	fi
done
# The wparted icons were installed into hicolor, so every cache must be rebuilt:
# GTK ignores icons that are missing from an existing, stale cache.
for theme_dir in "$DIST"/share/icons/*/; do
	gtk-update-icon-cache -f -q "$theme_dir"
done


# --- GTK and GLib translations for the languages WParted ships -----------------

echo "==> Bundling GTK translations"
for lang_dir in "$DIST"/share/locale/*/; do
	lang=$(basename "$lang_dir")
	for domain in gtk30 gtk30-properties glib20; do
		mo="$PREFIX/share/locale/$lang/LC_MESSAGES/$domain.mo"
		if [[ -f "$mo" ]]; then
			cp -f "$mo" "$DIST/share/locale/$lang/LC_MESSAGES/"
		fi
	done
done


# --- Licenses and documentation --------------------------------------------------

echo "==> Adding licenses and documentation"
mkdir -p "$DIST/licenses"
cp -f COPYING "$DIST/LICENSE.txt"
cp -f README.md "$DIST/README.md"
cp -f AUTHORS "$DIST/AUTHORS.txt"

THIRD_PARTY="$DIST/licenses/THIRD-PARTY.txt"
cat > "$THIRD_PARTY" <<'TEXT'
WParted is free software under the GNU General Public License version 2 or
later, see LICENSE.txt.  Source code: https://github.com/AA-EION/WParted

The bin, lib and share folders also contain libraries and data from the MSYS2
project (GTK, GLib, gtkmm, cairo, pango, the Adwaita icon theme, ...), mostly
under the LGPL.  Their source code is available from https://packages.msys2.org
and https://github.com/msys2/MINGW-packages.  Bundled packages:

TEXT

if command -v pacman > /dev/null; then
	declare -A pkgs=()
	for f in "$DIST"/bin/*.dll "$DIST"/bin/*.exe "$DIST/$LOADERS_REL"/*.dll; do
		name=$(basename "$f")
		if [[ "$f" == "$DIST/$LOADERS_REL"/* ]]; then
			src="$PREFIX/$LOADERS_REL/$name"
		else
			src="$PREFIX/bin/$name"
		fi
		[[ -f "$src" ]] || continue
		pkg=$(pacman -Qqo "$src" 2> /dev/null || true)
		if [[ -n "$pkg" ]]; then
			pkgs[$pkg]=1
		fi
	done
	for extra in adwaita-icon-theme hicolor-icon-theme; do
		if pacman -Qq "$PKG_PREFIX-$extra" > /dev/null 2>&1; then
			pkgs[$PKG_PREFIX-$extra]=1
		fi
	done
	for pkg in $(printf '%s\n' "${!pkgs[@]}" | sort); do
		pacman -Q "$pkg" >> "$THIRD_PARTY"
		short=${pkg#"$PKG_PREFIX-"}
		if [[ -d "$PREFIX/share/licenses/$short" ]]; then
			cp -rf "$PREFIX/share/licenses/$short" "$DIST/licenses/"
		fi
	done
else
	echo "(package list unavailable: pacman not found)" >> "$THIRD_PARTY"
fi

echo "==> Bundle ready: $DIST"
du -sh "$DIST" 2> /dev/null || true
