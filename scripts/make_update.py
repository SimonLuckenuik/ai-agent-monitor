"""Writes an update file (spec 048): the files the firmware reads from storage (/sys/),
then the firmware image, in the format include/update_bundle.h reads.

    python scripts/make_update.py --image firmware.bin.gz --file index.html.gz=PATH ... --out X.update

scripts/build_options.py calls files_hash() and make_update() for every build.
"""
import argparse
import re
import struct

FILE_MAX = 256 * 1024
NAME = re.compile(r"^[a-z0-9_-][a-z0-9._-]{0,30}$")

_TABLE = []
for i in range(256):
    c = i << 24
    for _ in range(8):
        c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
    _TABLE.append(c & 0xFFFFFFFF)


def crc32(data, crc=0xFFFFFFFF):
    """The ESP8266 core's crc32() (and elf2bin's): MSB first, no final xor."""
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _TABLE[((crc >> 24) ^ b) & 0xFF]
    return crc


def files_hash(files):
    """What /sys/manifest records and the image compiles in (FILES_HASH): the CRC of every
    file's name, size and data, in the order given."""
    crc = 0xFFFFFFFF
    for name, data in files:
        crc = crc32(name.encode("ascii") + b"\0" + struct.pack("<I", len(data)) + data, crc)
    return crc


def make_update(files, image):
    if not 1 <= len(files) <= 16:
        raise ValueError("an update holds 1 to 16 files")
    out = bytearray(b"AMUP" + struct.pack("<BBHI", 1, len(files), 0, files_hash(files)))
    for name, data in files:
        if not NAME.match(name):
            raise ValueError("bad file name: %r" % name)
        if len(data) > FILE_MAX:
            raise ValueError("%s is over %d bytes" % (name, FILE_MAX))
        out += struct.pack("<B", len(name)) + name.encode("ascii") + struct.pack("<II", len(data), crc32(data)) + data
    return bytes(out + image)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", required=True)
    ap.add_argument("--file", action="append", default=[], metavar="NAME=PATH")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    files = []
    for spec in a.file:
        name, path = spec.split("=", 1)
        with open(path, "rb") as f:
            files.append((name, f.read()))
    with open(a.image, "rb") as f:
        data = make_update(files, f.read())
    with open(a.out, "wb") as f:
        f.write(data)
    print("make_update: %s, %d bytes, files hash %08x" % (a.out, len(data), files_hash(files)))


if __name__ == "__main__":
    main()
