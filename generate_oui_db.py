#!/usr/bin/env python3
"""
generate_oui_db.py — Download IEEE OUI list and generate binary lookup files.

Output:
  oui.dat        (115 KB)  — Sorted 3-byte OUI prefixes for binary search
  ouinames.dat   (977 KB)  — Prefixes + manufacturer names (optional)

Usage:
  python3 generate_oui_db.py [--output-dir ./build]
  Copy oui.dat (and optionally ouinames.dat) to Pico's LittleFS.
"""

import re, struct, sys, os, urllib.request

OUI_URL = "https://standards-oui.ieee.org/oui/oui.txt"

def download():
    print(f"Downloading {OUI_URL}...")
    req = urllib.request.Request(OUI_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as r:
        return r.read().decode("utf-8", errors="ignore")

def parse(content):
    ouis = {}
    pattern = re.compile(r'([0-9A-F]{6})\s+\(base 16\)\s+(.+)')
    for match in pattern.finditer(content):
        oui = match.group(1)
        name = match.group(2).strip()
        if '(' in name:
            name = name.split('(')[0].strip()
        if len(name) > 40:
            name = name[:37] + '...'
        ouis[oui] = name
    return ouis

def generate(output_dir, ouis):
    sorted_ouis = sorted(ouis.items())
    count = len(sorted_ouis)

    # oui.dat — prefixes only
    path1 = os.path.join(output_dir, "oui.dat")
    with open(path1, "wb") as f:
        f.write(struct.pack('<I', count))
        for oui_hex, name in sorted_ouis:
            f.write(bytes.fromhex(oui_hex))
    sz = os.path.getsize(path1)
    print(f"  {path1}: {sz:,} bytes ({sz/1024:.1f} KB)")

    # ouinames.dat — prefixes + string table
    path2 = os.path.join(output_dir, "ouinames.dat")
    with open(path2, "wb") as f:
        f.write(struct.pack('<I', count))
        names_offset = 8 + count * 3
        f.write(struct.pack('<I', names_offset))
        for oui_hex, name in sorted_ouis:
            f.write(bytes.fromhex(oui_hex))
        for oui_hex, name in sorted_ouis:
            f.write(name.encode('ascii', errors='replace') + b'\x00')
    sz = os.path.getsize(path2)
    print(f"  {path2}: {sz:,} bytes ({sz/1024:.1f} KB)")

    print(f"\nTotal OUIs: {count:,}")
    print(f"Memory for RAM cache: {count * 3 / 1024:.0f} KB")
    name_lens = [len(n) for _, n in sorted_ouis]
    print(f"Name lengths: avg={sum(name_lens)/len(name_lens):.1f} max={max(name_lens)}")

if __name__ == "__main__":
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "./build"
    os.makedirs(output_dir, exist_ok=True)

    content = download()
    ouis = parse(content)
    generate(output_dir, ouis)
