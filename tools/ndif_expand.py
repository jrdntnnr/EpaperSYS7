"""Expand an uncompressed NDIF (Disk Copy 6) image into a raw HFS volume.

Input is the data fork plus its resource fork, either raw or AppleDouble
(the `._name` / `name.rsrc` file written by unar). Only raw and zero-fill
chunks are handled; compressed (ADC/KenCode) images are rejected.

    bcem chunk (12 bytes): u24 sector, u8 type, u32 data offset, u32 length
"""
import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
APPLEDOUBLE_MAGIC = b"\x00\x05\x16\x07"
APPLEDOUBLE_RSRC_ID = 2
BCEM_HEADER = 128
CHUNK_RAW = 0x02
CHUNK_ZERO = 0x00
CHUNK_END = 0xFF
CHUNK_SIZE = 12


def resource_fork(path: Path) -> bytes:
    b = path.read_bytes()
    if b[:4] != APPLEDOUBLE_MAGIC:
        return b

    # AppleDouble: entry table at 26, 12 bytes per entry (id, offset, length).
    count = struct.unpack(">H", b[24:26])[0]
    for i in range(count):
        eid, off, ln = struct.unpack(">III", b[26 + 12 * i:38 + 12 * i])
        if eid == APPLEDOUBLE_RSRC_ID:
            return b[off:off + ln]
    raise SystemExit("AppleDouble file has no resource fork entry")


def find_bcem(rf: bytes) -> bytes:
    data_off, map_off = struct.unpack(">II", rf[:8])
    type_list = map_off + struct.unpack(">H", rf[map_off + 24:map_off + 26])[0]
    ntypes = struct.unpack(">H", rf[type_list:type_list + 2])[0] + 1

    for t in range(ntypes):
        typ, cnt, ref_off = struct.unpack(">4sHH", rf[type_list + 2 + 8 * t:type_list + 10 + 8 * t])
        if typ != b"bcem":
            continue
        entry = type_list + ref_off
        res_off = struct.unpack(">I", rf[entry + 4:entry + 8])[0] & 0xFFFFFF
        ln = struct.unpack(">I", rf[data_off + res_off:data_off + res_off + 4])[0]
        return rf[data_off + res_off + 4:data_off + res_off + 4 + ln]
    raise SystemExit("No bcem resource: not an NDIF image")


def chunks(bcem: bytes):
    for k in range(BCEM_HEADER, len(bcem), CHUNK_SIZE):
        word, off, ln = struct.unpack(">III", bcem[k:k + CHUNK_SIZE])
        yield word >> 8, word & 0xFF, off, ln


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("data", type=Path, help="NDIF data fork")
    p.add_argument("rsrc", type=Path, help="resource fork, raw or AppleDouble")
    p.add_argument("out", type=Path, help="raw volume image to write")
    a = p.parse_args()

    data = a.data.read_bytes()
    entries = list(chunks(find_bcem(resource_fork(a.rsrc))))
    if not entries or entries[-1][1] != CHUNK_END:
        raise SystemExit("bcem map has no terminator")
    total = entries[-1][0] * SECTOR

    with a.out.open("wb") as out:
        out.truncate(total)
        for sector, kind, off, ln in entries:
            if kind == CHUNK_RAW:
                out.seek(sector * SECTOR)
                out.write(data[off:off + ln])
            elif kind not in (CHUNK_ZERO, CHUNK_END):
                raise SystemExit(f"Unsupported chunk type 0x{kind:02x} at sector {sector}")

    print(f"Wrote {total} bytes ({total // SECTOR} sectors) to {a.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
