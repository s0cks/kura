#!/bin/sh
set -u
kurac="${1:?usage: run.sh path/to/kurac path/to/runtime-dir [rpath...]}"
runtime="${2:?usage: run.sh path/to/kurac path/to/runtime-dir [rpath...]}"
shift 2
dir="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
status=0

rpaths="-Wl,-rpath,$runtime"
for extra in "$@"; do
    rpaths="$rpaths -Wl,-rpath,$extra"
done

if ! "$kurac" "$dir/exports.kui" --emit-obj "$tmp/exports.o" --emit-header "$tmp/exports.h" >"$tmp/kurac.log" 2>&1; then
    echo "FAIL  exports.kui (does not compile)"
    grep -E "^error|syntax" "$tmp/kurac.log" | head -5
    exit 1
fi
# shellcheck disable=SC2086
if ! "${CC:-cc}" -I"$tmp" "$dir/host.c" "$tmp/exports.o" -o "$tmp/host" -L"$runtime" -lkura-runtime -lm $rpaths 2>"$tmp/cc.log"; then
    echo "FAIL  host.c (does not build)"
    head -10 "$tmp/cc.log"
    exit 1
fi

if out="$("$tmp/host" 2>&1)" && [ "$out" = "ok" ]; then
    echo "ok    host calls every export"
else
    echo "FAIL  host calls every export"
    echo "$out" | sed 's/^/      /'
    status=1
fi

for case in "overflow:doesn't fit the result of \`too_big\`" "byte:doesn't fit the result of \`byte_of\`"; do
    arg="${case%%:*}"
    want="${case#*:}"
    out="$("$tmp/host" "$arg" 2>&1)"
    code=$?
    if [ "$code" -ne 0 ] && echo "$out" | grep -qF -- "$want"; then
        echo "ok    $arg panics"
    else
        echo "FAIL  $arg (wanted a panic: $want)"
        echo "$out" | sed 's/^/      /'
        status=1
    fi
done
exit $status
