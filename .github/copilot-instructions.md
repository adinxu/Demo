# Terminal Discovery Copilot Instructions

## Scope & Focus
- Stay inside `C++/Windows/CodeBlocks/TerminalDiscovery` unless the user widens scope; other folders are unrelated samples.
- Core deliverable is the ARP-driven terminal discovery daemon: C for runtime/adapter, C++ only for the northbound bridge plus integration tests.
- Preserve existing CLI/log phrasing; project documentation remains in Chinese unless the target file already uses another language.

## Architecture & Data Flow
- `src/main/terminal_main.c` wires configuration (`td_config`), adapter registry, manager, and netlink, sets log level, and handles SIGINT/SIGTERM (shutdown) plus SIGUSR1 (stats dump).
- `src/common/terminal_manager.c` is the state machine (FNV buckets under `mgr->lock`, worker thread, `terminal_manager_flush_events`, `max_terminals` guard) that emits events and owns stats.
- `src/common/terminal_netlink.c` maintains address/prefix state from NETLINK_ROUTE events via `iface_prefix_add/remove` and calls back into the manager.
- `src/common/terminal_northbound.cpp` + `src/include/terminal_discovery_api.hpp` expose `setIncrementReport`/`getAllTerminalInfo`, convert to `TerminalInfo`, and fetch the active manager via `terminal_manager_get_active`.
- Switch MAC access flows through the Realtek bridge C API; `src/stub/td_switch_mac_stub.c` + `tests/td_switch_mac_stub_tests.c` define the test double.

## Adapter & Interface Handling
- `src/adapter/realtek_adapter.c` opens AF_PACKET sockets, installs `attach_arp_filter`, enables `PACKET_AUXDATA`, throttles TX with `td_adapter_config.tx_interval_ms`, restores VLAN tags, and falls back from physical-interface VLAN tagging to bound VLAN subinterfaces if needed.
- Register adapters via `src/adapter/adapter_registry.c` (`td_adapter_descriptor` list) and honor the `td_adapter_ops` contract in `src/include/adapter_api.h`.
- Probe callbacks from the manager issue `td_adapter_arp_request` objects; keep fallback-to-physical-iface logic aligned with `terminal_main` and reuse helpers like `iface_record_select_ip` instead of rolling new selection paths.

## Runtime Conventions
- All timing uses `CLOCK_MONOTONIC`; rely on `monotonic_now`/`timespec_diff_ms` helpers and keep long operations out of the worker critical section.
- Only treat interfaces as usable when `tx_ifindex > 0` and a matching prefix exists; `resolve_tx_interface` plus `iface_record_prune_if_empty` guard invariants.
- `setIncrementReport` is single-shot and must return `-EALREADY` on subsequent registrations; callbacks should remain non-blocking.
- Update `terminal_manager_stats` (queried via CLI and tests) whenever lifecycle transitions occur, and keep new logs at INFO/DEBUG so test expectations at ERROR level stay quiet.

## Build & Test
- From `src/`, `make` builds `terminal_discovery`; artifacts land beside the Makefile.
- `make test` builds and runs `terminal_discovery_tests` (state machine), `terminal_integration_tests` (northbound bridge), and `td_switch_mac_stub_tests` (bridge stub contract).
- Cross builds: `make cross` uses the Realtek `mips-rtl83xx` toolchain; `make cross-generic` targets other MIPS prefixes, or override with `TOOLCHAIN_PREFIX=...`/`CROSS=...`.
- `src/demo/` (e.g., `stage0_raw_socket_demo.c`) documents platform behavior—treat as reference only, keep production changes isolated.

## Process & Documentation
- Follow the opt-in `/spec` → `/plan` → `/do` workflow from `AGENTS.md` when triggered; write specs under `specs/` and plans under `plans/YYYY-MM-DD-title.md`.
- Use `apply_patch` for edits, keep diffs narrow, and respect ASCII encoding unless a file already uses non-ASCII.
- Route adapter/runtime logs through `td_log_writef` or the provided adapter sink to preserve formatting.
- Mirror surrounding language (Chinese by default) and reuse mermaid when extending docs that already use it.
