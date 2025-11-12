# Terminal Discovery Copilot Instructions

## Scope & Focus
- Stay inside `C++/Windows/CodeBlocks/TerminalDiscovery`; other repo folders are historic demos.
- Production sources live in `src/`; `src/ref/` only stores vendor reference code and should not be modified for features.
- Codebase is ANSI C except `src/common/terminal_northbound.cpp` and `src/tests/terminal_integration_tests.cpp`.
- Documentation defaults to Chinese—follow the surrounding language unless a file is already English.

## Architecture & Flow
- `src/main/terminal_main.c` loads defaults via `td_config`, parses CLI flags (`--adapter`, `--tx-interval`, `--stats-interval`, `--max-terminals`, etc.), configures logging/signals, and orchestrates adapter + manager + netlink startup and shutdown.
- `src/common/terminal_manager.c` keeps terminals in 256 hash buckets under `mgr->lock`, queues events until `terminal_manager_flush_events`, and exposes stats through `terminal_manager_get_stats`.
- Worker thread `terminal_manager_worker` ticks on `CLOCK_MONOTONIC`, drives `terminal_manager_on_timer`, schedules keepalives, prunes IFACE_INVALID entries after holdoff, and runs probe/MAC refresh queues outside the main lock.
- `src/common/terminal_netlink.c` listens on `NETLINK_ROUTE`, tracks per-iface prefixes, updates `tx_source_ip` bindings, and demotes terminals on RTM_DELADDR.
- `src/common/terminal_northbound.cpp` provides the sole C++ bridge: `setIncrementReport` allows just one callback (`-EALREADY` on repeat) and `getAllTerminalInfo` snapshots via `terminal_manager_query_all` while holding the manager lock.

## Manager & Events
- Register event sinks with `terminal_manager_set_event_sink`; call `terminal_manager_flush_events` in tests/CLI paths to emit queued notifications synchronously.
- Keep heavy work outside `mgr->lock`; the manager already batches events and probe tasks under lock and completes them afterwards.
- VLAN decisions flow through `resolve_tx_interface`, optional `iface_selector` hooks, and `iface_record_select_ip`; preserve existing log strings because tests grep `[switch-mac-stub]` and similar markers.
- Terminal lifecycle is ACTIVE → PROBING → IFACE_INVALID based on keepalive timers; probe requests capture fallback iface info and update `probes_scheduled` / `probe_failures` counters.

## Adapter & Integration
- `src/adapter/realtek_adapter.c` wraps AF_PACKET, installs an ARP-only BPF filter, enables `PACKET_AUXDATA` so VLAN IDs arrive via `tpacket_auxdata`, and rate-limits TX with `send_lock` + `last_send` vs `tx_interval_ms`.
- Adapter contracts sit in `src/include/adapter_api.h`; implementations register through `adapter_registry.c` and should wire mac-locator hooks for keepalive lookups.
- `src/stub/td_switch_mac_stub.c` mocks the switch bridge; tests set `TD_SWITCH_MAC_STUB_COUNT` and expect deterministic ASCII tables and `[switch-mac-stub]` logs.

## Build & Test
- Build from `src`: `make` produces `terminal_discovery`, `make clean` purges artifacts.
- `make test` runs `terminal_manager_tests`, `terminal_integration_tests`, and `td_switch_mac_stub_tests`; most failures stem from event sequencing or netlink state.
- Cross builds: `make cross` targets Realtek `mips-rtl83xx`; override with `TOOLCHAIN_PREFIX` or `CROSS=...` when using other toolchains.
- Logs must go through `td_log_writef`; avoid new ERROR-level lines or format drift because tests assert exact strings.

## Process & Docs
- `/spec → /plan → /do` workflow is opt-in but mandatory once triggered; specs belong in `specs/`, plans in `plans/YYYY-MM-DD-title.md`.
- Use `apply_patch` for edits, keep diffs tight, and respect any existing staged changes instead of reverting user work.
- Prefer Chinese prose for docs/comments by default and stick to ASCII unless the file already mixes encodings.
