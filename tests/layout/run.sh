#!/bin/sh
# Runs every program in programs/ with `kura --run view` on the headless renderer and compares the rects
# its view filled with the program's .expected file. The first line of a program says what to call it
# with (`// args: 1`). Pass --update to write the .expected files instead of comparing.
#
#   tests/layout/run.sh path/to/kura [--update]
set -u
kura="${1:?usage: run.sh path/to/kura [--update]}"
update="${2:-}"
dir="$(cd "$(dirname "$0")" && pwd)/programs"
status=0

for f in "$dir"/*.kui; do
    name="$(basename "$f" .kui)"
    args="$(sed -n 's|^// args: ||p' "$f" | head -n 1)"
    # shellcheck disable=SC2086
    out="$("$kura" "$f" --dump-draw --run view $args 2>&1)" || { echo "FAIL  $name (did not run)"; echo "$out" | sed 's/^/      /'; status=1; continue; }
    got="$(echo "$out" | grep '^fill-rect')"
    if [ "$update" = "--update" ]; then
        echo "$got" > "$dir/$name.expected"
        echo "wrote $name.expected"
    elif [ "$got" = "$(cat "$dir/$name.expected")" ]; then
        echo "ok    $name"
    else
        echo "FAIL  $name"
        echo "      expected:"; sed 's/^/        /' "$dir/$name.expected"
        echo "      got:"; echo "$got" | sed 's/^/        /'
        status=1
    fi
done
exit $status
