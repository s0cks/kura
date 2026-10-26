#!/usr/bin/env python3
"""
Usage: python3 run_tests.py path/to/antlr-complete.jar
Needs: java (JRE) and `pip install antlr4-python3-runtime==<same version as jar>`.

Generates a Python parser from the grammars, then checks that every file in
tests/valid parses with zero syntax errors and every file in tests/invalid
produces at least one.
"""
import subprocess, sys, tempfile, shutil, importlib, pathlib

here = pathlib.Path(__file__).parent
jar = sys.argv[1]
out = pathlib.Path(tempfile.mkdtemp())
for g in ("KuraLexer.g4", "KuraParser.g4"):
    shutil.copy(here.parent.parent / "Sources" / g, out / g)
subprocess.run(["java", "-cp", jar, "org.antlr.v4.Tool", "-Dlanguage=Python3",
                "KuraLexer.g4", "KuraParser.g4"], cwd=out, check=True)
sys.path.insert(0, str(out))

from antlr4 import FileStream, CommonTokenStream
from antlr4.error.ErrorListener import ErrorListener
KuraLexer = importlib.import_module("KuraLexer").KuraLexer
KuraParser = importlib.import_module("KuraParser").KuraParser


class Collect(ErrorListener):
    def __init__(self): self.errors = []
    def syntaxError(self, rec, sym, line, col, msg, e):
        self.errors.append(f"{line}:{col} {msg}")


def parse(path):
    c = Collect()
    lexer = KuraLexer(FileStream(str(path), encoding="utf-8"))
    lexer.removeErrorListeners(); lexer.addErrorListener(c)
    parser = KuraParser(CommonTokenStream(lexer))
    parser.removeErrorListeners(); parser.addErrorListener(c)
    parser.source()
    return c.errors


failed = 0
for f in sorted((here / "valid").glob("*.kura")):
    errs = parse(f)
    print(("FAIL" if errs else "ok  "), "(valid)  ", f.name)
    for e in errs: print("      ", e)
    failed += bool(errs)
for f in sorted((here / "invalid").glob("*.kura")):
    errs = parse(f)
    print(("ok  " if errs else "FAIL"), "(invalid)", f.name, "->", errs[0] if errs else "parsed cleanly")
    failed += not errs
sys.exit(1 if failed else 0)
