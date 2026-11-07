# Shared presentation helper for external component builders. Outside the
# quiet make session, commands retain their ordinary stdout/stderr and status.
build_component() {
    build_label=$1
    shift
    if [ "${V:-0}" = 1 ] || [ -z "${BUILD_STATUS_SESSION:-}" ]; then
        "$@"
    else
        python3 "$root/src/tools/build_status.py" --step "$build_label" -- "$@"
    fi
}
