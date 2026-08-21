# ThreadDeck Android parity checklist

The Android tablet is a remote ThreadDeck client, not a reduced viewer. A feature is complete only when it operates on the Ubuntu host, preserves ThreadDeck's per-thread/per-project state, and reports the same result on tablet and desktop.

## Workspace and navigation

- [x] Private WireGuard host connection and reconnect; ADB is deployment-only
- [x] Project names, paths, thread assignments, custom thread titles, and themes
- [x] Project collapse/expand and global collapse/expand
- [x] Cross-project thread search
- [ ] Show/hide navigation pane
- [x] Add project, rename project, and delete empty project
- [x] Project sorting and per-project thread sorting
- [x] Rename, summarize/title, move, and delete thread
- [ ] Busy, approval-needed, completed-unseen, and failed thread indicators

## Thread session

- [x] Read transcript, resume writable thread, active-writer fallback, and automatic handoff retry
- [x] Send, steer/follow up, stop, streaming output, and approvals
- [x] Per-thread YOLO policy and Shield propagation
- [x] Shared tablet mutation channel for ThreadDeck state
- [x] Tappable access and Shield controls with YOLO confirmation
- [x] Model and reasoning catalogs/selectors
- [x] Pause at safe checkpoint and continue
- [x] Automatic command-copy toggle
- [ ] Remote Shield host selection and credential management
- [x] Token/rate-limit/account usage strip
- [x] Context/details inspector

## Composer and transcript

- [x] Per-thread text drafts and multiline follow-ups
- [x] Image attachment picker, previews, upload, and removal
- [x] Audio attachment picker, previews, upload, and removal
- [ ] Slash-skill discovery and completion
- [ ] Composer undo/redo and prompt history navigation
- [ ] Rich Markdown rendering, links, expandable activity, and one-tap code copying
- [ ] Scroll-to-bottom control and transcript media rendering

## Application tools

- [x] All desktop theme palettes and system theme synchronization
- [ ] Project Instructions browser/editor for AGENTS.md
- [ ] Splunk host/token keyring settings
- [ ] About/version information
- [ ] Responsive replacements for desktop pane sizing and window-only controls

## Tablet connection architecture

ThreadDeck is a remote client. Codex and project files remain on the Ubuntu host.

- The tablet's `ThreadDeck` WireGuard tunnel uses `10.77.0.3/32` and routes only `10.77.0.1/32`. Normal tablet traffic does not use the tunnel.
- Android's always-on VPN setting keeps WireGuard available across app changes and tablet restarts. VPN lockdown is intentionally off because this is a split tunnel.
- `threaddeck-tablet-bridge.service` listens only on `10.77.0.1:4545` and accepts only `10.77.0.3`.
- The Android app connects to `ws://10.77.0.1:4545`. The WebSocket is carried inside WireGuard's encrypted, mutually authenticated tunnel.
- `threaddeck-tablet-adb.service` is disabled. Wireless debugging is needed only when installing a new APK or collecting diagnostics.

If the tablet cannot connect, check the `ThreadDeck` tunnel in WireGuard, then check the host with `sudo wg show wg0` and `systemctl --user status threaddeck-tablet-bridge.service`. No ADB reverse rule should be required.
