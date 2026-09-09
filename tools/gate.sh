#!/usr/bin/env bash
# The local gate, with short output. Full logs stay in build-core/gate.log and
# build-plugin/gate.log; the terminal gets one line per step and, on a failure, the
# failing lines.
#
#   tools/gate.sh                 core and plugin: the full gate before a push
#   tools/gate.sh core            configure, build, run the core tests
#   tools/gate.sh core -t driver  only the core tests with a Catch2 tag (list: build-core/chipboy_tests --list-tags)
#   tools/gate.sh plugin          plugin targets, plugin checks, the parameter table,
#                                 the link test under a 1 MB (Windows-sized) stack
#
# CHIPBOY_JUCE_DIR: a local JUCE 8.0.15 clone, to skip the fetch. JOBS: parallel level.
set -u
cd "$(dirname "$0")/.."
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
LAUNCHER=(); command -v ccache >/dev/null 2>&1 && LAUNCHER=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
JUCE=(); [ -n "${CHIPBOY_JUCE_DIR:-}" ] && JUCE=(-DFETCHCONTENT_SOURCE_DIR_JUCE="$CHIPBOY_JUCE_DIR")
what=${1:-all}; [ $# -gt 0 ] && shift
tag=""
while [ $# -gt 0 ]; do
    case $1 in
        -t) tag=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
fail=0

run() {   # run NAME LOG CMD...: one line; on failure the log's failing lines
    local name=$1 log=$2; shift 2
    local t0=$SECONDS
    printf '=== %s: %s\n' "$name" "$*" >> "$log"
    if "$@" >> "$log" 2>&1; then
        printf 'ok    %-26s %5ds\n' "$name" $((SECONDS - t0))
    else
        printf 'FAIL  %-26s %5ds\n' "$name" $((SECONDS - t0)); fail=1
        grep -n -E 'error|Error|FAILED|Failed|\*\*\*|Assertion|fault|mismatch' "$log" | tail -40
        printf '      full log: %s\n' "$log"
    fi
}
summary() {   # the ctest totals line, indented
    grep -E 'tests passed|tests failed' "$1" | tail -1 | sed 's/^/      /'
}
warnings() {   # ChipBoy's own targets stay warning-free; JUCE's are not ours
    local log=$1
    local lines; lines=$(grep -E '^[^ ]+:[0-9]+:[0-9]+: warning:' "$log" | grep -v -E '_deps/|/juce_|/JUCE/' || true)
    if [ -n "$lines" ]; then
        printf 'WARN  %-26s\n' "$(basename "$(dirname "$log")")"; echo "$lines" | head -20; fail=1
    fi
}

core() {
    mkdir -p build-core; local log=build-core/gate.log; : > "$log"
    run "core configure" "$log" cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release "${LAUNCHER[@]}"
    run "core build" "$log" cmake --build build-core --parallel "$JOBS"
    warnings "$log"
    if [ -n "$tag" ]; then
        run "core tests [$tag]" "$log" ./build-core/chipboy_tests "[$tag]"
        grep -E 'All tests passed|test cases:|No test' "$log" | tail -1 | sed 's/^/      /'
    else
        run "core tests" "$log" ctest --test-dir build-core --output-on-failure
        summary "$log"
    fi
}

plugin() {
    mkdir -p build-plugin; local log=build-plugin/gate.log; : > "$log"
    run "plugin configure" "$log" cmake -S . -B build-plugin -DCHIPBOY_BUILD_PLUGIN=ON \
        -DCHIPBOY_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release "${LAUNCHER[@]}" "${JUCE[@]}"
    run "plugin build" "$log" cmake --build build-plugin --config Release --parallel "$JOBS" --target \
        ChipBoy_VST3 ChipBoyVoice_VST3 ChipBoy_Standalone \
        chipboy_linktest chipboy_recordtest chipboy_paramdump chipboy_uishot chipboy_fuzz
    warnings "$log"
    run "plugin checks" "$log" ctest --test-dir build-plugin -C Release --output-on-failure
    summary "$log"
    run "parameter table" "$log" python3 tools/demo/make_demo.py \
        --paramdump build-plugin/chipboy_paramdump_artefacts/Release/chipboy_paramdump
    run "link test, 1 MB stack" "$log" bash -c \
        'ulimit -s 1024 && exec ./build-plugin/chipboy_linktest_artefacts/Release/chipboy_linktest'
}

case $what in
    core) core ;;
    plugin) plugin ;;
    all) core; plugin ;;
    *) echo "usage: tools/gate.sh [core|plugin|all] [-t tag]" >&2; exit 2 ;;
esac
if [ $fail -eq 0 ]; then echo "GATE GREEN"; else echo "GATE RED"; fi
exit $fail
