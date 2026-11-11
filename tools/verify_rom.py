"""Identify supplied compact Mac ROMs; does not download or redistribute them."""
import argparse
import hashlib
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("rom",type=Path)
a=p.parse_args()
b=a.rom.read_bytes()
if len(b) not in (131072,262144,524288):
    raise SystemExit("Expected 128KB Plus, 256KB SE or 512KB Classic ROM")
print("Bytes:",len(b))
print("ROM identifier:",b[:4].hex().upper())
print("SHA256:",hashlib.sha256(b).hexdigest())
print("Size/header inspection only; emulator verifies supported ROM patches at startup.")
