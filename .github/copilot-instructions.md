# Terminal Discovery Copilot Instructions

## Scope & Focus
- Stay inside `C++/Windows/CodeBlocks/Terminal_Discovery` unless the user explicitly broadens scope; other folders are unrelated samples.
- Core deliverable is an ARP-based terminal discovery daemon written in C with POSIX threading; C++ is only for the northbound bridge and integration tests.
- Preserve the existing CLI/log output style; new documentation under this project should remain in Chinese unless the target file already uses another language.

## Architecture & Data Flow
- `src/main/terminal_main.c` drives startup: parses CLI flags, loads defaults via `td_config`, wires adapter/manager/netlink, sets log level, and reacts to SIGINT/SIGTERM (shutdown) plus SIGUSR1 (stats dump).
- `src/common/terminal_manager.c` is the state machine: FNV-hashed buckets, `mgr->lock` for mutations, `worker_lock`/thread for timers, event batching, and `max_terminals` enforcement.
- `src/common/terminal_netlink.c` runs a dedicated thread on NETLINK_ROUTE, feeding IPv4 add/del into `terminal_manager_on_address_update` and relying on `iface_prefix_add/remove` and `iface_record_prune_if_empty`.
- `src/common/terminal_northbound.cpp` plus `src/include/terminal_discovery_api.hpp` expose `getAllTerminalInfo`/`setIncrementReport`, translating `terminal_event_record_t` to `TerminalInfo` and grabbing the active manager with `terminal_manager_get_active`.
- `src/include/td_config.h` + `src/common/td_config.c` define runtime defaults; extend both runtime and manager configs together when adding knobs.

## Adapter Layer
- `src/adapter/realtek_adapter.c` is the production adapter: opens AF_PACKET sockets, installs `attach_arp_filter`, enables `PACKET_AUXDATA`, enforces pacing with `td_adapter_config.tx_interval_ms`, and handles VLAN tagging with fallbacks.
- Adapter descriptors live in `src/adapter/adapter_registry.c`; new adapters must supply a `td_adapter_descriptor`, register it in the static table, and honor the `td_adapter_ops` contract in `src/include/adapter_api.h`.
- Probe callbacks in `terminal_main` translate `terminal_probe_request_t` to `td_adapter_arp_request`; keep fallback-to-physical-iface logic aligned with the existing branch.

## Runtime Patterns
- Dispatch events with `terminal_manager_flush_events` or via the background worker; keep event queue mutations under `mgr->lock` and preserve `queue_modify_event_if_port_changed` semantics (only MOD on port change).
- All timing uses `CLOCK_MONOTONIC`; reuse `monotonic_now`/`timespec_diff_ms` helpers rather than `gettimeofday`.
- Maintain interface binding invariants: only treat an interface as available when `tx_ifindex > 0` and a matching prefix exists; use `iface_record_select_ip` and related helpers instead of reimplementing selection.
- `setIncrementReport` is single-shot; return `-EALREADY` on subsequent registrations and ensure the callback remains non-blocking.
- Update `terminal_manager_stats` alongside lifecycle transitions so CLI stats and tests stay consistent.

## Build & Test
- From `src/`, `make` builds the daemon and adapter; outputs land alongside the Makefile.
- `make test` runs both `terminal_manager_tests` (pure C with stub adapter) and `terminal_integration_tests` (C++ bridge). Tests assume `td_log_level` is ERROR—keep new logs at INFO/DEBUG.
- Cross builds: `make cross` for the Realtek `mips-rtl83xx` toolchain or `make cross-generic` for other MIPS prefixes; override with `TOOLCHAIN_PREFIX=...` when needed.
- `src/demo/Makefile` + `stage0_raw_socket_demo.c` are reference-only; avoid coupling production changes to the demo.

## Process & Collaboration
- Follow the opt-in `/spec` → `/plan` → `/do` flow from `AGENTS.md` when the user issues those commands; specs belong in `specs/`, plans in `plans/YYYY-MM-DD-title.md`.
- Use `apply_patch` for edits, keep diffs minimal, and respect existing encoding (default to ASCII).
- Honor existing logging tone by routing adapter logs through `td_log_writef` or the adapter env sink.
- When updating docs, mirror the surrounding language (Chinese by default) and reuse mermaid for diagrams if the file already does so.
