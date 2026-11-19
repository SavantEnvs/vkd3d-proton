#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

HARNESSES="fuzz_compile_dxbc fuzz_root_signature fuzz_rs_raw fuzz_signature fuzz_dxil_library"
# Device-backed harness: also links the d3d12 implementation (libvkd3d-proton.a), run on lavapipe.
DEVICE_HARNESSES="fuzz_pipeline_library"
FUZZ_BUILD="$SRC/build.fuzz"     # gitignored by upstream's `build.*`
TEST_BUILD="$SRC/build.test"

# meson's vcs_tag runs `git describe`; under a different uid than the tree owner git refuses.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'

# setup_or_reconfigure <dir> <c_args> <link_args>: idempotent meson configure (re-run safe).
setup_or_reconfigure() {
  local dir="$1" cflags="$2" ldflags="$3" act=setup
  [ -f "$dir/build.ninja" ] && act="setup --reconfigure"
  # shellcheck disable=SC2086
  meson $act "$dir" "$SRC" --buildtype=plain --wrap-mode=nodownload -Denable_extras=true \
    -Dc_args="$cflags" -Dcpp_args="$cflags" -Dc_link_args="$ldflags" -Dcpp_link_args="$ldflags"
}

# 1) The project itself, sanitized + SanCov-instrumented (fuzzer-no-link) with DWARF 3, in two trees:
#    build.fuzz   -- the shader translation libraries (vkd3d-shader, vkd3d-common, dxil-spirv,
#                    dxbc-spirv): the blob parsers behind the five shader/root-signature harnesses.
#    build.device -- the same plus the d3d12 implementation (libvkd3d-proton.a + d3d12core's objects)
#                    for the device-backed fuzz_pipeline_library harness.
# UBSan relaxations (ASan and every other UBSan check stay on and halting):
#  * both trees: -fno-sanitize=function. vkd3d implements COM in C; every vtable call
#    (ID3D12Device_QueryInterface(...) -> d3d12_device_QueryInterface(struct d3d12_device *)) goes through
#    a pointer typed with the interface pointer, which this check flags on the first API call.
#  * build.device only: -fno-sanitize=array-bounds. libs/vkd3d/cache.c declares its blob chunks
#    `data[] __counted_by(size)` and finish_and_iterate_blob_chunk() zeroes the alignment padding past
#    `size` (inside the allocation) on every serialization, so the check fires on valid input.
FUZZ_BASE_CFLAGS="-O1 $SANITIZER_FLAGS -fno-sanitize=function $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
DEVICE_BUILD="$SRC/build.device"
OBJ_TARGETS=(libs/d3d12core/libvkd3d-proton-d3d12core.so.p/main.c.o libs/d3d12core/libvkd3d-proton-d3d12core.so.p/debug.c.o)

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$SRC/build.fuzz.lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$SRC/build.fuzz.standalone_main.o"

# build_tree <dir> <cflags> <device:0|1> <harness...>: configure + build the project, then link each
# harness twice (libFuzzer binary + standalone reproducer), LSan off (mayhem/lsan_off.cc).
build_tree() {
  local dir="$1" cflags="$2" device="$3"; shift 3
  local targets=(programs/vkd3d-rs-parse/vkd3d-rs-parse) gen libs link h incs
  [ "$device" = 1 ] && targets+=(libs/vkd3d/libvkd3d-proton.a "${OBJ_TARGETS[@]}")
  setup_or_reconfigure "$dir" "$cflags" "$SANITIZER_FLAGS"
  # The widl-generated public headers (vkd3d_d3d12.h, ...) the harnesses include.
  mapfile -t gen < <(ninja -C "$dir" -t targets all | sed -n 's|^\(include/[^:]*\.h\): .*|\1|p')
  ninja -C "$dir" -j"$MAYHEM_JOBS" "${targets[@]}" "${gen[@]}"
  # Static libs the shader library depends on (vkd3d-shader -> vkd3d-common, dxil-spirv -> dxbc-spirv).
  mapfile -t libs < <(find "$dir/libs/vkd3d-shader" "$dir/libs/vkd3d-common" "$dir/subprojects" -name '*.a' | sort)
  link=("${libs[@]}")
  # d3d12core's main.c + debug.c hold the vkd3d_debug_control_* hooks libvkd3d-proton.a calls
  # (upstream ships them as libvkd3d-proton-d3d12core.so); link their objects statically.
  [ "$device" = 1 ] && link=("${OBJ_TARGETS[@]/#/$dir/}" "$dir/libs/vkd3d/libvkd3d-proton.a" "${libs[@]}")
  incs=(-I"$SRC/include" -I"$SRC/include/private" -I"$dir/include" -I"$dir/include/private"
        -I"$SRC/khronos/Vulkan-Headers/include" -I"$SRC/khronos/SPIRV-Headers/include" -I"$SRC/mayhem/harnesses")
  mkdir -p "$dir/mayhem-obj"
  for h in "$@"; do
    $CC $cflags -std=c11 -D_GNU_SOURCE "${incs[@]}" -c "$SRC/mayhem/harnesses/$h.c" -o "$dir/mayhem-obj/$h.o"
    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$dir/mayhem-obj/$h.o" "$SRC/build.fuzz.lsan_off.o" \
      -Wl,--start-group "${link[@]}" -Wl,--end-group -lpthread -ldl -lm -o "$SRC/$h"
    $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$dir/mayhem-obj/$h.o" "$SRC/build.fuzz.standalone_main.o" "$SRC/build.fuzz.lsan_off.o" \
      -Wl,--start-group "${link[@]}" -Wl,--end-group -lpthread -ldl -lm -o "$SRC/$h-standalone"
  done
}

# shellcheck disable=SC2086
build_tree "$FUZZ_BUILD" "$FUZZ_BASE_CFLAGS" 0 $HARNESSES
# shellcheck disable=SC2086
build_tree "$DEVICE_BUILD" "$FUZZ_BASE_CFLAGS -fno-sanitize=array-bounds" 1 $DEVICE_HARNESSES

# 3) Test oracle: upstream's own vkd3d-rs-parse tool, built with the project's NORMAL flags.
setup_or_reconfigure "$TEST_BUILD" "-O2 $COVERAGE_FLAGS" "$COVERAGE_FLAGS"
ninja -C "$TEST_BUILD" -j"$MAYHEM_JOBS" programs/vkd3d-rs-parse/vkd3d-rs-parse
