# Frontend golden tests

`kura_dump.cc` parses a source file and prints the module (types, top-level
bindings, functions) through `ExprPrinter`. `golden/<name>.txt` is the expected
output for `examples/<name>.kui`.

Build `kura_dump` against `kura-runtime` and compare:

    for f in examples/*.kui; do
      kura_dump "$f" | diff - "tests/frontend/golden/$(basename "$f" .kui).txt"
    done

`tests/grammar` holds syntax-only tests for the ANTLR grammars (`run_tests.py`).
