#!/bin/sh
# Lowers every program in this directory to a flow graph, types it, and checks
# the result. kurac verifies each graph (terminators, phis vs predecessors,
# dominance of every use) and exits non-zero when a program can't be lowered, is
# malformed, or doesn't type check.
#
# Programs in errors/ must be rejected: the first line is `// error: <text>`,
# and <text> must appear in the diagnostics.
#
#   tests/backend/run.sh path/to/kurac
set -u
kurac="${1:?usage: run.sh path/to/kurac}"
dir="$(cd "$(dirname "$0")" && pwd)"
status=0

for f in "$dir"/*.kura; do
  if out="$("$kurac" "$f" 2>&1)"; then
    echo "ok    $(basename "$f")"
  else
    echo "FAIL  $(basename "$f")"
    echo "$out" | grep -E "error" | sed 's/^/      /'
    status=1
  fi
done

for f in "$dir"/errors/*.kura; do
  want="$(head -n 1 "$f" | sed -n 's|^// error: ||p')"
  if out="$("$kurac" "$f" 2>&1)"; then
    echo "FAIL  errors/$(basename "$f") (accepted)"
    status=1
  elif echo "$out" | grep -qF -- "$want"; then
    echo "ok    errors/$(basename "$f")"
  else
    echo "FAIL  errors/$(basename "$f") (wanted: $want)"
    echo "$out" | grep -E "error" | sed 's/^/      /'
    status=1
  fi
done

# Programs in run/ are compiled and executed. Each `// run: <fn> [args] => <value>`
# line calls a function and compares what it returns, as `kurac --run` prints
# it. Each `// panic: <fn> [args] => <text>` line must abort with <text>.
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
for f in "$dir"/run/*.kura; do
  name="run/$(basename "$f")"
  grep -hE '^// (run|panic): ' "$f" > "$tmp"
  if ! "$kurac" "$f" --emit-llvm > /dev/null 2> "$tmp.err"; then
    echo "FAIL  $name (does not compile)"
    sed 's/^/      /' "$tmp.err" | head -5
    status=1
    continue
  fi
  pass=0
  while IFS= read -r line; do
    kind="${line#// }"; kind="${kind%%:*}"
    spec="${line#// $kind: }"
    call="${spec%% => *}"
    want="${spec#* => }"
    # shellcheck disable=SC2086
    out="$("$kurac" "$f" --run $call 2>&1)"; code=$?
    if [ "$kind" = run ]; then
      got="$(echo "$out" | sed -n 's/^=> //p' | head -n 1)"
      if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
      else
        echo "FAIL  $name: $call"
        echo "      wanted: $want"
        echo "      got:    ${got:-$(echo "$out" | grep -E 'error|panic' | head -n 1)}"
        status=1
      fi
    else
      if [ "$code" -ne 0 ] && echo "$out" | grep -qF -- "$want"; then
        pass=$((pass + 1))
      else
        echo "FAIL  $name: $call (wanted a panic: $want)"
        status=1
      fi
    fi
  done < "$tmp"
  echo "ok    $name ($pass checks)"
done
rm -f "$tmp.err"
exit $status
