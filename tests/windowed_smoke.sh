#!/usr/bin/env bash

# Run a short-lived desktop sample under Xvfb. Xvfb startup can occasionally
# fail on a shared runner; retry once while preserving the real application
# exit status and diagnostics.
set -u

if [[ $# -lt 2 ]]; then
    echo "usage: $0 <name> <command> [args...]" >&2
    exit 2
fi

name=$1
shift
log_dir="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/lumen-windowed-smoke"
mkdir -p "$log_dir"

status=1
for attempt in 1 2; do
    output="$log_dir/${name}-${attempt}.log"
    xvfb_error="$log_dir/${name}-${attempt}.xvfb.log"
    xvfb-run -a -e "$xvfb_error" timeout 5 "$@" >"$output" 2>&1
    status=$?

    if [[ $status -eq 124 ]]; then
        cat "$output"
        exit 0
    fi

    echo "windowed smoke '$name' attempt $attempt exited with status $status" >&2
    if [[ -s "$output" ]]; then
        cat "$output" >&2
    fi
    if [[ -s "$xvfb_error" ]]; then
        cat "$xvfb_error" >&2
    fi
    if [[ $attempt -eq 1 ]]; then
        echo "retrying windowed smoke '$name' once" >&2
    fi
done

exit "$status"
