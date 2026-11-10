#!/usr/bin/env python3
"""
Syntax tests for Kura's hand-written parser.
Every file in valid/ must parse with no diagnostics at all, and every file in invalid/ must be rejected.
Usage: python3 run_tests.py path/to/kura-syntax-dump

kura-syntax-dump is built by CMake (target `kura-syntax-dump`, from tests/syntax) and needs nothing but
the C++ standard library; there is no parser generator or Java involved any more. The unit tests in
tests/syntax (`kura-syntax-tests`, run by ctest) go much deeper: error recovery, spans, fuzzing. This
script is the quick end-to-end check over the sample programs.
"""

import subprocess, sys, tempfile, shutil, importlib, pathlib

import pathlib
import subprocess
import sys

if len(sys.argv) != 2:
    sys.exit(__doc__)
dump = sys.argv[1]
root = pathlib.Path(__file__).resolve().parent


def parse(path):
    """Returns (exit status, diagnostics) for one file."""
    proc = subprocess.run([dump, str(path)], capture_output=True, text=True)
    return proc.returncode, proc.stderr


failures = 0
for expect_ok, folder in ((True, "valid"), (False, "invalid")):
    for path in sorted((root / folder).glob("*.kui")):
        status, diagnostics = parse(path)
        if status not in (0, 1):
            print(f"FAIL  {folder}/{path.name} (the dump tool failed: {status})")
            failures += 1
        elif expect_ok and (status != 0 or diagnostics):
            print(f"FAIL  {folder}/{path.name} (rejected)")
            print("      " + diagnostics.strip().replace("\n", "\n      "))
            failures += 1
        elif not expect_ok and status == 0:
            print(f"FAIL  {folder}/{path.name} (accepted)")
            failures += 1
        else:
            print(f"ok    {folder}/{path.name}")

sys.exit(1 if failures else 0)
