#!/usr/bin/env python3
"""Refuse a public release that carries anything from the ROMs (tools/make_release.sh).

  python tools/check_public.py ROMDIR FORBIDDEN_FILE... -- TREE_OR_ZIP...

ROMDIR: every file under it (recursive: ROM/xsleena, ROM/xsleenaba) is forbidden by
content (SHA-1) and by name; FORBIDDEN_FILE: more files forbidden by content (the
sample file XainSleena.snd, recorded from the ROMs). Also refused by name: any
*.zip, *.lha, *.snd, *.bin, *.cfg, *.key inside a public tree or ZIP (the ROM set,
the samples, configuration files with passwords). Exit code 1 and a list on stdout.
"""
import hashlib
import os
import sys
import zipfile

BAD_EXT = (".zip", ".lha", ".snd", ".bin", ".cfg", ".key")


def sha(b):
    return hashlib.sha1(b).hexdigest()


def main():
    args = sys.argv[1:]
    if "--" not in args or len(args) < 3:
        sys.exit(__doc__)
    i = args.index("--")
    romdir, forbidden, targets = args[0], args[1:i], args[i + 1:]

    bad_sums, bad_names = {}, set()
    for root, _, files in os.walk(romdir):
        for f in files:
            p = os.path.join(root, f)
            bad_sums[sha(open(p, "rb").read())] = p
            bad_names.add(f.lower())
    for p in forbidden:
        if os.path.isfile(p):
            bad_sums[sha(open(p, "rb").read())] = p
    if not bad_sums:
        sys.exit("check_public: no ROM files found in %s - cannot check" % romdir)

    problems = []

    def check(where, name, data):
        base = name.replace("\\", "/").rstrip("/").split("/")[-1].lower()
        if base in bad_names:
            problems.append("%s: %s - a ROM file name" % (where, name))
        elif base.endswith(BAD_EXT):
            problems.append("%s: %s - a forbidden kind of file" % (where, name))
        s = sha(data)
        if s in bad_sums:
            problems.append("%s: %s - the same as %s" % (where, name, bad_sums[s]))

    for t in targets:
        if os.path.isdir(t):
            for root, _, files in os.walk(t):
                for f in files:
                    p = os.path.join(root, f)
                    check(t, os.path.relpath(p, t), open(p, "rb").read())
        elif t.lower().endswith(".zip") and zipfile.is_zipfile(t):
            with zipfile.ZipFile(t) as z:
                for info in z.infolist():
                    if not info.is_dir():
                        check(t, info.filename, z.read(info))
        elif os.path.isfile(t):
            check(t, os.path.basename(t), open(t, "rb").read())
        else:
            problems.append("%s: missing" % t)

    for p in problems:
        print("check_public:", p)
    if problems:
        sys.exit(1)
    print("check_public: no ROMs in", ", ".join(targets))


if __name__ == "__main__":
    main()
