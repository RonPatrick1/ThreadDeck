#!/usr/bin/env bash

set -u

tablet_serial="${THREADDECK_TABLET_SERIAL:-192.168.0.26:40409}"
tablet_mdns_prefix="${THREADDECK_TABLET_MDNS_PREFIX:-adb-R5GL351F5QL-}"
tablet_port="${THREADDECK_TABLET_PORT:-4545}"
retry_seconds="${THREADDECK_TABLET_RETRY_SECONDS:-5}"

connected_tablet() {
    if [[ "$(adb -s "$tablet_serial" get-state 2>/dev/null || true)" == "device" ]]; then
        printf '%s\n' "$tablet_serial"
        return 0
    fi

    while IFS=$'\t' read -r candidate state; do
        if [[ "$candidate" == "$tablet_mdns_prefix"* && "$state" == device* ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done < <(adb devices 2>/dev/null)

    return 1
}

discover_tablet() {
    adb connect "$tablet_serial" >/dev/null 2>&1 || true

    local connected
    connected="$(connected_tablet || true)"
    if [[ -n "$connected" ]]; then
        printf '%s\n' "$connected"
        return 0
    fi

    while IFS=$'\t' read -r service_name service_type endpoint; do
        if [[ "$service_name" != "$tablet_mdns_prefix"* || "$service_type" != "_adb-tls-connect._tcp" ]]; then
            continue
        fi

        adb connect "$endpoint" >/dev/null 2>&1 || true
        if [[ "$(adb -s "$endpoint" get-state 2>/dev/null || true)" == "device" ]]; then
            printf '%s\n' "$endpoint"
            return 0
        fi
    done < <(adb mdns services 2>/dev/null)

    return 1
}

while true; do
    active_serial="$(connected_tablet || discover_tablet || true)"

    if [[ -n "$active_serial" ]]; then
        reverse_rules="$(adb -s "$active_serial" reverse --list 2>/dev/null || true)"

        if ! grep -Eq "tcp:${tablet_port}[[:space:]]+tcp:${tablet_port}$" <<<"$reverse_rules"; then
            adb -s "$active_serial" reverse \
                "tcp:${tablet_port}" "tcp:${tablet_port}" \
                >/dev/null 2>&1 || true
        fi
    fi

    sleep "$retry_seconds"
done
