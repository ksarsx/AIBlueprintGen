#!/usr/bin/env python3
"""
Builds dist/AIBlueprintGen-Setup.exe: a single self-extracting installer (works without PowerShell scripts lying around).

    pip install ziglang
    python Installer/build_exe.py

The exe = stub (SelfExtract/stub.c, compiled with zig cc) + zip of the repository + a small trailer.
Works on Windows, Linux and macOS.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
STUB_C = os.path.join(ROOT, "Installer", "SelfExtract", "stub.c")
OUT_EXE = os.path.join(DIST, "AIBlueprintGen-Setup.exe")

INCLUDE = [
    ("AIBlueprintGen", True),
    ("Installer/Install-AIBlueprintGen.ps1", False),
    ("Installer/Install.bat", False),
    ("LICENSE", False),
    ("README.md", False),
]
SKIP_DIRS = {"Binaries", "Intermediate", "Saved", ".vs", "__pycache__"}


def build_stub(stub_exe):
    # --subsystem windows: a GUI program, no console window
    cmd = [sys.executable, "-m", "ziglang", "cc", "-target", "x86_64-windows-gnu", "-O2", "-s",
           "-Wl,--subsystem,windows", "-o", stub_exe, STUB_C]
    print(" ".join(cmd))
    subprocess.check_call(cmd)


def build_payload(payload_zip):
    with zipfile.ZipFile(payload_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, is_dir in INCLUDE:
            path = os.path.join(ROOT, *rel.split("/"))
            if not is_dir:
                z.write(path, rel)
                continue
            for folder, dirs, files in os.walk(path):
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
                for name in files:
                    full = os.path.join(folder, name)
                    z.write(full, os.path.relpath(full, ROOT).replace(os.sep, "/"))


def main():
    os.makedirs(DIST, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        stub_exe = os.path.join(work, "stub.exe")
        payload_zip = os.path.join(work, "payload.zip")
        build_stub(stub_exe)
        build_payload(payload_zip)
        with open(payload_zip, "rb") as f:
            payload = f.read()
        with open(stub_exe, "rb") as f:
            stub = f.read()
    with open(OUT_EXE, "wb") as f:
        f.write(stub)
        f.write(payload)
        f.write(struct.pack("<Q", len(payload)))
        f.write(b"ABGPAYLD")
    print("Done: %s (%.1f KB)" % (OUT_EXE, os.path.getsize(OUT_EXE) / 1024))


if __name__ == "__main__":
    main()
