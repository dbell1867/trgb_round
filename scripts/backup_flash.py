#!/usr/bin/env python3
"""Robust full-flash backup for ESP32 boards on native USB-Serial/JTAG.

esptool's fast stub reader can stall at fixed, content-dependent blocks
("Packet content transfer stopped"). Retrying the whole read never helps,
so we change method instead (skill gotcha G42):

  1 MB chunks (stub)  ->  failed chunk: 64 KB pieces (stub)
                      ->  failed piece: 4 KB blocks (ROM reader, --no-stub)

Usage: backup_flash.py OUT.bin [--port /dev/ttyACM0] [--size 0x1000000]
Then confirm with:  esptool --port PORT verify-flash 0 OUT.bin
"""
import argparse
import os
import subprocess
import sys
import tempfile

CHUNK = 0x100000   # 1 MB
PIECE = 0x10000    # 64 KB
BLOCK = 0x1000     # 4 KB (flash sector)


def read(port, addr, size, path, stub=True, tries=1):
    """Read [addr, addr+size) into path. Returns True on success."""
    cmd = ["esptool", "--chip", "esp32s3", "--port", port]
    if not stub:
        cmd.append("--no-stub")
    cmd += ["read-flash", hex(addr), hex(size), path]
    for _ in range(tries):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0 and os.path.getsize(path) == size:
            return True
    return False


def read_range(port, addr, size, step, tmp, log):
    """Read a range in `step`-sized parts, descending a level on failure."""
    out = bytearray()
    for a in range(addr, addr + size, step):
        part = os.path.join(tmp, f"{a:08x}.bin")
        if read(port, a, step, part, stub=True):
            out += open(part, "rb").read()
            continue
        if step > PIECE:
            log(f"  stub failed at {a:#010x} ({step // 1024} KB) -> 64 KB pieces")
            out += read_range(port, a, step, PIECE, tmp, log)
        elif step > BLOCK:
            log(f"  stub failed at {a:#010x} (64 KB) -> 4 KB ROM reads")
            for b in range(a, a + step, BLOCK):
                blk = os.path.join(tmp, f"{b:08x}_rom.bin")
                if read(port, b, BLOCK, blk, stub=False, tries=3):
                    out += open(blk, "rb").read()
                else:
                    sys.exit(f"FAILED even with ROM reader at {b:#010x}")
                log(f"    ROM read ok {b:#010x}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x1000000)
    args = ap.parse_args()

    data = bytearray()
    with tempfile.TemporaryDirectory() as tmp:
        for addr in range(0, args.size, CHUNK):
            print(f"chunk {addr:#010x}", flush=True)
            data += read_range(args.port, addr, CHUNK, CHUNK, tmp,
                               lambda m: print(m, flush=True))
    assert len(data) == args.size, len(data)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"wrote {args.out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
