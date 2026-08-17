#!/usr/bin/env bash

set -u

old_pid="${1:-}"
threaddeck_executable="/var/www/CodexNativeUbuntu/build/threaddeck"

if [[ ! "$old_pid" =~ ^[0-9]+$ ]]; then
    exit 2
fi

running_executable="$(readlink "/proc/${old_pid}/exe" 2>/dev/null || true)"
if [[ "$running_executable" != "$threaddeck_executable" && "$running_executable" != "$threaddeck_executable (deleted)" ]]; then
    exit 3
fi

descendants=()

collect_descendants() {
    local parent_pid="$1"
    local child_pid

    while read -r child_pid; do
        if [[ "$child_pid" =~ ^[0-9]+$ ]]; then
            collect_descendants "$child_pid"
            descendants+=("$child_pid")
        fi
    done < <(pgrep -P "$parent_pid" 2>/dev/null || true)
}

collect_descendants "$old_pid"
kill -TERM "$old_pid" "${descendants[@]}" 2>/dev/null || true

for attempt in 1 2 3 4 5 6 7 8; do
    still_running=()

    for process_id in "$old_pid" "${descendants[@]}"; do
        if kill -0 "$process_id" 2>/dev/null; then
            still_running+=("$process_id")
        fi
    done

    if (( ${#still_running[@]} == 0 )); then
        break
    fi

    sleep 1
done

for process_id in "$old_pid" "${descendants[@]}"; do
    if kill -0 "$process_id" 2>/dev/null; then
        kill -KILL "$process_id" 2>/dev/null || true
    fi
done

sleep 2
exec "$threaddeck_executable"
