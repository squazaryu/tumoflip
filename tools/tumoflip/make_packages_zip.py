#!/usr/bin/env python3
"""Emit tumoflip-packages.zip next to tumoflip-packages.json.

The companion iOS app (squazaryu/unleashed-companion, issue #8) installs SD package
files atomically (stage -> verify -> rename, with rollback). It reads the schema-v2
manifest and needs the raw file bytes. Rather than have the app decode the firmware's
heatshrink (HSDS) resources archive, we publish a plain DEFLATE zip containing exactly
the manifest's `source` files; each zip entry path equals its manifest `source` 1:1.

Run this in the release step, after validate_release.py has written the manifest, and
upload the resulting zip as a release asset alongside tumoflip-packages.json.

Usage:
    python3 tools/tumoflip/make_packages_zip.py <manifest.json> <resources_root> <out.zip>

<resources_root> is the uncompressed firmware resources tree (e.g.
build/f7-firmware-*/resources) where each manifest `source` resolves to
<resources_root>/<source>.
"""
import json
import os
import sys
import zipfile
import hashlib


def main():
    if len(sys.argv) != 4:
        sys.stdout.write(__doc__)
        sys.exit(2)
    manifest_path, resources_root, out_zip = sys.argv[1:4]

    manifest = json.load(open(manifest_path))
    if manifest.get("schema") != 2:
        sys.exit("unsupported manifest schema: %r" % manifest.get("schema"))

    sources = []
    for _group, files in manifest.get("packages", {}).items():
        for entry in files:
            sources.append((entry["source"], entry["sha256"]))

    missing, mismatched = [], []
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        for source, sha in sources:
            path = os.path.join(resources_root, source)
            if not os.path.isfile(path):
                missing.append(source)
                continue
            data = open(path, "rb").read()
            if hashlib.sha256(data).hexdigest() != sha:
                mismatched.append(source)
                continue
            zf.writestr(source, data)

    if missing:
        sys.exit("missing sources (%d):\n  %s" % (len(missing), "\n  ".join(missing)))
    if mismatched:
        sys.exit("sha256 mismatch (%d):\n  %s" % (len(mismatched), "\n  ".join(mismatched)))
    sys.stdout.write("wrote %s: %d files\n" % (out_zip, len(sources)))


if __name__ == "__main__":
    main()
