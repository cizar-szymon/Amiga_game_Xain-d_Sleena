#!/bin/sh
# The release of the Xain'd Sleena port (after sh tools/mk.sh and
# sh tools/mk.sh -f Makefile.amiga), number from XAIN_VERSION in port/src/xain.h.
# Layout (the standard of the Settlers / OpenDUNE ports, 3.11.2026):
#   releases/amiga/XainSleena/...           the public Amiga release, in git: the program,
#                                           its icon (port/amiga/XainSleena.info), doc/ with
#                                           all the texts (descriptions PL/EN, dev PL/EN,
#                                           release history PL/EN) and its drawer icon
#                                           doc.info, an empty rom/ with a ReadMe
#   releases/amiga/XainSleena.info          the drawer's icon
#   releases/amiga/XainSleena-<v>.zip       the same as an archive - no ROMs
#   releases/pc/XainSleena/...              the public PC release, in git: XainSleena.exe,
#                                           SDL2.dll, doc/, an empty rom/ with a ReadMe
#   releases/pc/XainSleena-<v>-pc.zip       the same as an archive - no ROMs
#   private/XainSleena-<v>-full.zip         the Amiga release + the ROMs (rom/xsleena/,
#                                           from ROM/xsleena) + XainSleena.snd (samples
#                                           recorded from the ROMs): runs at once
#   private/XainSleena-<v>-pc-full.zip      the PC release + rom/xsleena.zip + XainSleena.snd
#   (private/ is only on this PC, not in git; older full packages go to private/old/)
# The archives are ZIPs (tools/make_zip.py, Unix permissions: the program 755, the rest
# 644); on the Amiga:  UnZip XainSleena-<v>.zip -d Work:Games
# The public trees and ZIPs are checked for ROMs (tools/check_public.py): any ROM file
# (by name or content), the samples or a zip inside stops the release.
#   sh tools/make_release.sh [--no-full]
set -e
cd "$(dirname "$0")/.."
V=$(sed -n 's/^#define XAIN_VERSION "\(.*\)"/\1/p' port/src/xain.h)
[ -n "$V" ] || { echo "XAIN_VERSION not found in port/src/xain.h" >&2; exit 1; }
FULL=1
for a in "$@"; do
	case "$a" in
		--no-full) FULL=0 ;;
	esac
done
G=XainSleena
A=releases/amiga
P=releases/pc
ROMDIR=ROM/xsleena
SND=build/port/$G.snd

# the binaries must be of this release (Amiga $VER, PC window title)
#   has FILE TEXT [nul]   TEXT in FILE (with "nul": followed by a 0 byte, the end of a C string)
has() { python -c "import sys; t = sys.argv[2].encode('latin-1') + (b'\0' if len(sys.argv) > 3 else b''); sys.exit(0 if t in open(sys.argv[1], 'rb').read() else 1)" "$@"; }
has build/amiga/$G "\$VER: $G $V (" || { echo "build/amiga/$G is not $V: sh tools/mk.sh -f Makefile.amiga first" >&2; exit 1; }
has build/port/$G.exe "Xain'd Sleena $V" nul || { echo "build/port/$G.exe is not $V: sh tools/mk.sh first" >&2; exit 1; }

# the public trees (made again from the sources in git)
rm -rf "$A/$G" "$P/$G"
mkdir -p "$A/$G/doc" "$A/$G/rom" "$P/$G/doc" "$P/$G/rom"
cp build/amiga/$G "$A/$G/$G"
cp port/amiga/$G.info "$A/$G/$G.info"
for f in $G.txt ${G}_en.txt ${G}_dev.txt ${G}_dev_en.txt ${G}_release.txt ${G}_release_en.txt; do
	cp "docs/release/amiga/$f" "$A/$G/doc/$f"
	cp "docs/release/pc/$f" "$P/$G/doc/$f"
done
cp docs/release/amiga/rom_ReadMe.txt "$A/$G/rom/ReadMe.txt"
cp docs/release/pc/rom_ReadMe.txt "$P/$G/rom/ReadMe.txt"
# the drawer icons: the game's drawer and doc (a plain drawer, tools/icon/drawer.png)
cp tools/icon/warp-drawer.info "$A/$G.info"	# the game drawer: the WARP logo (tools/icon/warp-drawer.info, ../WARP-drawer.info)
python -W ignore::DeprecationWarning tools/make_doc_icon.py "$A/$G/doc.info" --frame tools/icon/drawer.png --crop 0 0 46 40 --transparent --drawer >/dev/null
cp build/port/$G.exe "$P/$G/$G.exe"
cp build/port/SDL2.dll "$P/$G/SDL2.dll"

# the archives; an older release's ZIP leaves git and goes to private/old/
mkdir -p private/old
for f in "$A"/$G-*.zip "$P"/$G-*.zip; do
	[ -e "$f" ] || continue
	[ "$f" = "$A/$G-$V.zip" ] || [ "$f" = "$P/$G-$V-pc.zip" ] && continue
	git rm -q --cached "$f" 2>/dev/null || true
	mv "$f" private/old/
done
python tools/make_zip.py "$A/$G-$V.zip" "$A" $G $G.info
python tools/make_zip.py "$P/$G-$V-pc.zip" "$P" $G
python tools/check_public.py ROM "$SND" -- "$A/$G" "$P/$G" "$A/$G.info" "$A/$G-$V.zip" "$P/$G-$V-pc.zip" || \
	{ echo "ROMs in the public release - stopped: the public ZIPs must not be committed (fix and run again)" >&2; exit 1; }

if [ "$FULL" = 1 ]; then
	[ -d "$ROMDIR" ] || { echo "$ROMDIR missing: no full packages" >&2; exit 1; }
	[ -f "$SND" ] || { echo "$SND missing: no full packages" >&2; exit 1; }
	for f in private/$G-*-full.zip; do
		[ -e "$f" ] || continue
		[ "$f" = "private/$G-$V-full.zip" ] || [ "$f" = "private/$G-$V-pc-full.zip" ] || mv "$f" private/old/
	done
	mkdir -p build/release
	python tools/make_zip.py build/release/xsleena.zip ROM --add "$ROMDIR" . >/dev/null	# the MAME set as a zip, for the PC
	python tools/make_zip.py "private/$G-$V-full.zip" "$A" $G $G.info \
		--add "$ROMDIR" "$G/rom/xsleena" --add "$SND" "$G/$G.snd"
	python tools/make_zip.py "private/$G-$V-pc-full.zip" "$P" $G \
		--add build/release/xsleena.zip "$G/rom/xsleena.zip" --add "$SND" "$G/$G.snd"
fi
ls -l "$A/$G-$V.zip" "$P/$G-$V-pc.zip" private/$G-$V-full.zip private/$G-$V-pc-full.zip 2>/dev/null
