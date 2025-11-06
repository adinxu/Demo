# Terminal Discovery Copilot Instructions

## Overview
- Focus on `C++/Windows/CodeBlocks/Terminal_Discovery` unless user states otherwise; other folders are unrelated samples.
- Core goal: track network terminals via ARP, schedule keepalives, and expose snapshots/incremental events to northbound consumers.
- Default language is C with POSIX threading; C++ appears only in the northbound bridge and integration tests.

## Architecture Map
- `src/common/terminal_manager.c` holds the terminal state machine, timers, iface bookkeeping, and event queue.
- `src/adapter/realtek_adapter.c` is the only production adapter today; it wraps raw sockets, BPF filters, pacing, and VLAN handling.
- `src/common/terminal_netlink.c` monitors IPv4 address changes and updates `terminal_manager` iface bindings.
- `src/common/terminal_northbound.cpp` plus `src/include/terminal_discovery_api.hpp` bridge the manager to external C++ code via `getAllTerminalInfo` and `setIncrementReport`.
- `src/main/terminal_main.c` is the daemon wiring: parses config, instantiates the adapter, wires callbacks, runs until signalled.
- Specs and plans live under `specs/` and `plans/`; follow the Stage-Gated workflow in `AGENTS.md` when the user issues `/spec`, `/plan`, `/do`.

## Build & Test
- Build everything from `src/` with `make`; binaries land beside the Makefile.
- Run unit + integration tests via `make test` (executes `terminal_manager_tests` and `terminal_integration_tests`).
- Cross builds: `make cross` (Realtek `mips-rtl83xx` toolchain) or `make cross-generic` (generic MIPS prefix). Pass `TOOLCHAIN_PREFIX=...` if you need a custom cross compiler.

## Key Runtime Patterns
- Terminal capacity defaults to 1000 (`TERMINAL_DEFAULT_MAX_TERMINALS`); honor `terminal_manager_config.max_terminals` when adding features.
- Event delivery: only emit `MOD` when logical port changes (`queue_modify_event_if_port_changed`); keep batches small and dispatch outside the manager lock.
- Keep `terminal_manager` mutations under `mgr->lock`; timer thread uses `mgr->worker_lock` to serialize wakeups.
- Interface availability depends on both `tx_ifindex > 0` and a matching prefix in `iface_records`; use helpers like `iface_record_select_ip` instead of duplicating logic.
- All timed behavior uses `CLOCK_MONOTONIC`; match that when adding waits or comparisons.

## Adapter Expectations
- `td_adapter_ops` lives in `src/include/adapter_api.h`; new adapters must implement the same callbacks and respect pacing (`cfg.tx_interval_ms`).
- Realtek adapter assumes ARP ingress on the physical iface and inserts VLAN tags on egress; reuse `attach_arp_filter`, `PACKET_AUXDATA`, and pacing helpers for consistency.
- When sending ARP, prefer physical iface tagging; fall back to VLAN if the platform lacks VLAN insertion—mirror the guard clauses already present.

## Netlink & Address Bookkeeping
- Address updates feed `terminal_manager_on_address_update`; they maintain `iface_records` and detach stale bindings. Reuse `iface_prefix_add/remove` helpers.
- Removing the last prefix for an iface must trigger `iface_record_prune_if_empty`; this prevents dangling bindings.

## Northbound API Rules
- `setIncrementReport` may be called once; return `-EALREADY` on duplicates and keep the callback non-blocking.
- Always transform internal events to `TerminalInfo` via `format_mac`/`format_ip`; catch exceptions and log using `td_log_writef` instead of propagating.
- Before querying or emitting, fetch the active manager with `terminal_manager_get_active`; return `-ENODEV` if unavailable.

## Logging & Metrics
- Use `td_log_writef` with appropriate levels (`TD_LOG_*`); tests set log level to `ERROR`, so keep noisy logs at INFO/DEBUG.
- Update `terminal_manager_stats` counters in tandem with lifecycle changes; tests assert on `probes_scheduled`, `probe_failures`, etc.

## Testing Guidance
- Extend `tests/terminal_manager_tests.c` for pure C scenarios (use stub adapter/probe capture helpers provided).
- Use `tests/terminal_integration_tests.cpp` when exercising the C++ API surface and incremental callback path.
- Re-run `make test` after modifications; tests are fast and gate most regressions.

## Process Notes
- Honor the `/spec` → `/plan` → `/do` workflow whenever triggered; only code inside `/do`. Specs go in `specs/`, plans in `plans/` with `YYYY-MM-DD-title.md`.
- Default to `apply_patch` for edits, keep diffs scoped, and prefer ASCII unless a file already uses other encodings.