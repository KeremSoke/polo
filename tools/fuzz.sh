#!/bin/sh
# Fuzz every protocol group of tests/fuzz for a while, one libFuzzer process each.
#
#   tools/fuzz.sh [SECONDS] [BUILD-DIR] [GROUP...]
#
# SECONDS defaults to 3600 and BUILD-DIR to build-fuzz, configured with
#   CC=clang CXX=clang++ meson setup build-fuzz -Dfuzz=true -Dshell-ui=false \
#     -Dxwayland=false -Db_sanitize=address,undefined -Db_lundef=false
# and built with
#   ninja -C build-fuzz tests/fuzz/fuzz-protocols \
#     subprojects/weston/libweston/backend-headless/headless-backend.so
# Without GROUPs, all of them run at once. Work goes to fuzz-work/GROUP, crash
# inputs to fuzz-work/GROUP/crash-*; copy the ones worth keeping into
# tests/fuzz/corpus/GROUP and merge new coverage with
#   fuzz-protocols -merge=1 tests/fuzz/corpus/GROUP fuzz-work/GROUP
set -eu

seconds=${1:-3600}
build=${2:-build-fuzz}
[ $# -gt 2 ] && shift 2 || shift $#
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"

fuzzer="$build/tests/fuzz/fuzz-protocols"
weston="$here/$build/subprojects/weston/libweston"
[ -x "$fuzzer" ] || { echo "fuzz.sh: build $fuzzer first (see the header)" >&2; exit 1; }

interfaces() {
    case $1 in
        layer-shell) echo zwlr_layer_shell_v1 ;;
        foreign-toplevel) echo zwlr_foreign_toplevel_manager_v1,ext_foreign_toplevel_list_v1,xfce_foreign_toplevel_manager_private_v1 ;;
        workspace) echo ext_workspace_manager_v1 ;;
        session-lock) echo ext_session_lock_manager_v1 ;;
        data-control) echo zwlr_data_control_manager_v1,ext_data_control_manager_v1 ;;
        image-copy-capture) echo ext_output_image_capture_source_manager_v1,ext_foreign_toplevel_image_capture_source_manager_v1,ext_image_copy_capture_manager_v1 ;;
        *) echo "fuzz.sh: no group $1" >&2; return 1 ;;
    esac
}

groups=${*:-"layer-shell foreign-toplevel workspace session-lock data-control image-copy-capture"}

export WESTON_MODULE_MAP="headless-backend.so=$weston/backend-headless/headless-backend.so"
export LD_LIBRARY_PATH="$weston"

for group in $groups; do
    list=$(interfaces "$group")
    work="$here/fuzz-work/$group"
    mkdir -p "$work"
    cp -n "tests/fuzz/corpus/$group"/* "$work"/
    (
        cd "$work"
        POLO_FUZZ_PROTOCOLS=$list "$here/$fuzzer" "$work" -max_total_time="$seconds" -timeout=20 \
            -rss_limit_mb=4096 -print_final_stats=1 >"$work.log" 2>&1 || echo "$group: stopped with a finding, see $work.log"
    ) &
done
wait
for group in $groups; do
    echo "$group: $(grep -E 'Done .* runs' "$here/fuzz-work/$group.log" || echo 'no summary')"
done
