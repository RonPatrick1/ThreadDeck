# ThreadDeck Android parity checklist

The Android tablet is a remote ThreadDeck client, not a reduced viewer. A feature is complete only when it operates on the Ubuntu host, preserves ThreadDeck's per-thread/per-project state, and reports the same result on tablet and desktop.

## Workspace and navigation

- [x] Wireless host connection, reconnect, and persistent ADB reverse tunnel
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
