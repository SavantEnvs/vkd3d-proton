#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# EDIT: RUN the test runner that mayhem/build.sh produced, then map its output to counts.
#   ctest:        (cd build-tests && ctest) ; parse "<P> tests passed, <F> failed out of <T>"
#   gtest binary: ./build-tests/<prog> ; parse "[==========] N ... ran." / "[  PASSED  ] P" / "[ SKIPPED ] S"
#   make/minunit: ./out/<runner> ; parse its pass/fail/total
# Do NOT compile here — if the runner is absent, fail (build.sh should have produced it).

# Known-answer tests of upstream's own root-signature dumper (programs/vkd3d-rs-parse, built by build.sh
# with normal flags): each mayhem/testdata/rs-parse/*.rts0 is the RTS0 payload of a root signature from
# upstream's tests/d3d12_root_signature.c etc.; its dump (compat hash, flags, parameters, ranges, static
# samplers) must match the committed *.expected byte for byte. Plus negative tests: malformed payloads
# (truncated, garbage) must be rejected with a non-zero exit.
RS_PARSE="$SRC/build.test/programs/vkd3d-rs-parse/vkd3d-rs-parse"
[ -x "$RS_PARSE" ] || { echo "missing $RS_PARSE -- run mayhem/build.sh first" >&2; emit_ctrf vkd3d-rs-parse 0 1; exit 1; }
TD="$SRC/mayhem/testdata/rs-parse"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
passed=0 failed=0
for in in "$TD"/*.rts0; do
  name="$(basename "$in" .rts0)"
  if "$RS_PARSE" "$in" >"$tmp/out" 2>/dev/null && cmp -s "$tmp/out" "$TD/$name.expected"; then
    passed=$((passed+1))
  else
    echo "FAIL: $name (output differs from $name.expected)"; diff "$TD/$name.expected" "$tmp/out" | head -5
    failed=$((failed+1))
  fi
done
# Negative cases: a payload cut in half, and a payload whose parameter count points past the end.
first="$(ls "$TD"/*rootsig1*.rts0 | head -1)"
head -c "$(( $(stat -c %s "$first") / 2 ))" "$first" > "$tmp/truncated.rts0"
printf '\002\000\000\000\377\377\377\377\030\000\000\000' > "$tmp/garbage.rts0"
for neg in truncated garbage; do
  if "$RS_PARSE" "$tmp/$neg.rts0" >/dev/null 2>&1; then
    echo "FAIL: malformed payload '$neg' was accepted"; failed=$((failed+1))
  else
    passed=$((passed+1))
  fi
done
echo "vkd3d-rs-parse known-answer tests: $passed passed, $failed failed"
emit_ctrf "vkd3d-rs-parse-kat" "$passed" "$failed" 0
