#!/usr/bin/env bash
# Runs jqlite against valid and invalid inputs with AddressSanitizer,
# LeakSanitizer and UBSan enabled. Build first with:
#
#   gcc -std=c11 -Wall -Wextra -g -O0 -fsanitize=address,undefined src/jqlite.c -o bin/jqlite
#
# Usage: ./tests.sh            (set VERBOSE=1 to also show program output)

BIN="${BIN:-./bin/jqlite}"

# The sanitizers exit with status 1 by default, which is the same code the
# parser uses for a parse error. Give them a distinct code so a memory bug
# in an error path can't pass as an expected failure.
export ASAN_OPTIONS="detect_leaks=1:exitcode=99"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:exitcode=99"

valid=(
  # basic structure, empty containers, all scalar types
  '{"a": [1, 2.5, -0, 1e3, true, false, null], "b": {}, "c": []}'
  # every escape the parser supports
  '{"s": "q\" b\\ t\t n\n r\r f\f bs\b sl\/"}'
  # number formatting edge cases
  '[0.1, 1e308, 5e-324, 123456789012345678, -1.5e-10]'
  # nesting: objects inside arrays inside objects
  '{"x": [{"y": [[], {}, [{"z": null}]]}]}'
  # top-level scalars and surrounding whitespace
  '"just a string"'
  '  42  '
)

deep=$(printf '[%.0s' {1..200})$(printf ']%.0s' {1..200})

invalid=(
  # fails deep inside a partially built tree
  '{"a": 1, "b": [true, {"c": fals}]}'
  '{"a": [1, 2,'
  '{"a" 1}'
  '[1, 2] x'
  '"\u0041"'
  '1e999'
  '[01]'
  '-'
  ''
  # exceeds JSON_MAX_DEPTH
  "$deep"
)

pass=0
fail=0

report() { # status message [details]
  if [[ $1 == ok ]]; then
    pass=$((pass + 1))
    printf 'PASS  %s\n' "$2"
  else
    fail=$((fail + 1))
    printf 'FAIL  %s\n%s\n' "$2" "$3"
  fi
}

run() { # expected_exit input
  local expected=$1 input=$2 label out status
  label=$input
  (( ${#label} > 60 )) && label="${label:0:57}..."

  out=$(printf '%s' "$input" | "$BIN" 2>&1)
  status=$?
  [[ -n $VERBOSE ]] && printf '%s\n' "$out"

  if (( status != expected )); then
    report fail "$label" "  exit $status, expected $expected"$'\n'"$out"
    return
  fi

  # For valid input, feed the pretty-printed output back in: it should
  # parse and print identically.
  if (( expected == 0 )); then
    local again
    again=$(printf '%s\n' "$out" | "$BIN" 2>&1)
    if [[ $? -ne 0 || $again != "$out" ]]; then
      report fail "$label" "  second pass differs:"$'\n'"$again"
      return
    fi
  fi

  report ok "$label"
}

echo "== valid inputs (expect exit 0, idempotent output)"
for t in "${valid[@]}"; do run 0 "$t"; done

echo "== invalid inputs (expect exit 1)"
for t in "${invalid[@]}"; do run 1 "$t"; done

echo "== $pass passed, $fail failed"
(( fail == 0 ))
