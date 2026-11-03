#!/usr/bin/env python3
"""A release ZIP (tools/make_release.sh; copied from the Settlers port).

  python tools/make_zip.py OUT.zip BASE NAME... [--add PATH ARCPATH]...

NAME: files or directories under BASE, stored with their path from BASE.
--add PATH ARCPATH: the ROMs and the sample file of the full packages (only
those, never the public ones): a directory PATH - its files (not recursive)
stored under ARCPATH/ (ARCPATH "." = no directory: a flat MAME zip); a file
PATH - stored as ARCPATH.
Every directory gets its own entry (the empty rom drawer unpacks too).
Unix permissions (made-by Unix): programs 755, the rest 644 - Amiga UnZip turns
them into the protection bits, without them the program unpacks without E.
"""
import argparse
import os
import zipfile

EXECUTABLES = {"XainSleena", "XainSleena.exe"}


def put(z, path, arcname):
    info = zipfile.ZipInfo.from_file(path, arcname)
    info.create_system = 3
    mode = 0o755 if os.path.basename(arcname) in EXECUTABLES else 0o644
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    with open(path, "rb") as f:
        z.writestr(info, f.read())


def put_dir(z, arcname, done):
    arcname = arcname.rstrip("/") + "/"
    if arcname in done:
        return
    done.add(arcname)
    info = zipfile.ZipInfo(arcname)
    info.create_system = 3
    info.external_attr = (0o040755 << 16) | 0x10
    z.writestr(info, "")


def add_tree(z, base, name, done):
    path = os.path.join(base, name)
    if os.path.isfile(path):
        put(z, path, name.replace(os.sep, "/"))
        return
    for root, dirs, files in os.walk(path):
        dirs.sort()
        rel = os.path.relpath(root, base).replace(os.sep, "/")
        put_dir(z, rel, done)
        for f in sorted(files):
            put(z, os.path.join(root, f), rel + "/" + f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("base")
    ap.add_argument("names", nargs="*")
    ap.add_argument("--add", nargs=2, action="append", default=[], metavar=("PATH", "ARCPATH"))
    a = ap.parse_args()

    done = set()
    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in a.names:
            add_tree(z, a.base, n, done)
        for p, arc in a.add:
            if os.path.isfile(p):
                put(z, p, arc)
                continue
            prefix = "" if arc == "." else arc.rstrip("/") + "/"
            if prefix:
                put_dir(z, prefix, done)
            for f in sorted(os.listdir(p)):
                if os.path.isfile(os.path.join(p, f)):
                    put(z, os.path.join(p, f), prefix + f)
    print(a.out, os.path.getsize(a.out), "bytes")


if __name__ == "__main__":
    main()
