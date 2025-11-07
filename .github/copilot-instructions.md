# Terminal Discovery Copilot Instructions

## Scope & Focus
- Work inside `C++/Windows/CodeBlocks/TerminalDiscovery`; everything else in this repo is unrelated sample code.
- Daemon core is ANSI C; only `src/common/terminal_northbound.cpp` and the integration tests use C++.
- Documentation defaults to Chinese—match the surrounding language unless a file is already in English.

## Architecture & Flow
- `src/main/terminal_main.c` loads defaults via `td_config`, parses CLI flags (`--adapter`, `--tx-interval`, `--stats-interval`, `--max-terminals`, etc.), sets up logging, starts the chosen adapter + manager, launches netlink, and handles `SIGINT/SIGTERM` shutdown plus `SIGUSR1` stats dumps.
- `src/common/terminal_manager.c` owns the state machine: ARP packets become terminals hashed into 256 buckets under `mgr->lock`, events buffer until `terminal_manager_flush_events`, and stats are exposed through `terminal_manager_get_stats`.
- The worker thread (`terminal_manager_worker`) sleeps on `CLOCK_MONOTONIC` timers, invokes `terminal_manager_on_timer`, schedules keepalive probes, and prunes IFACE_INVALID terminals after holdoff intervals.
- `src/common/terminal_netlink.c` runs a NETLINK_ROUTE thread translating RTM_NEWADDR/DELADDR into `terminal_address_update_t` updates, tracking prefixes in `iface_records` and refreshing `tx_source_ip` bindings.
- `src/common/terminal_northbound.cpp` bridges to the C++ API: `setIncrementReport` installs a single callback (returns `-EALREADY` on repeats) and `getAllTerminalInfo` snapshots current entries via `terminal_manager_query_all`.

## Manager & Events
- Install event sinks with `terminal_manager_set_event_sink`; the dispatcher batches outside locks and updates `events_dispatched` / `event_dispatch_failures` counters.
- `resolve_tx_interface` consults optional `iface_selector` hooks before formatting VLAN subinterfaces (default `vlan%u`); bindings persist only when a prefix-derived source IP exists and `tx_kernel_ifindex > 0`.
- Terminals advance ACTIVE → PROBING → IFACE_INVALID via keepalive timers; probe requests capture fallback iface data and increment `probes_scheduled` / `probe_failures` stats.
- Netlink removals detach iface bindings, clear `tx_iface`, and push entries back to IFACE_INVALID while resetting `last_seen` so holdoff timers start immediately.
- Call `terminal_manager_flush_events` in tests/CLI paths to push queued notifications synchronously.

## Adapter & Interfaces
- `src/adapter/realtek_adapter.c` wraps AF_PACKET sockets: installs an ARP-only BPF filter, enables `PACKET_AUXDATA` so VLAN IDs arrive via `tpacket_auxdata`, polls in its own pthread, and throttles transmit timing with `send_lock` + `last_send` against `tx_interval_ms`.
- TX prefers the physical iface; on failure the manager retries with `tx_iface_valid=true` so VLAN subinterfaces become the fallback path.
- Adapter contracts live in `src/include/adapter_api.h`; register implementations through `adapter_registry.c` (currently only the Realtek descriptor).
- `src/stub/td_switch_mac_stub.c` is the switch bridge stub; tests adjust row counts through `TD_SWITCH_MAC_STUB_COUNT` and expect `[switch-mac-stub]` logs.

## Build & Test
- From `src/`, run `make` to produce `terminal_discovery`; `make clean` clears artifacts.
- `make test` builds and runs `terminal_discovery_tests` (state machine), `terminal_integration_tests` (northbound bridge), and `td_switch_mac_stub_tests` (stub contract).
- Cross builds use `make cross` (Realtek `mips-rtl83xx`) or `make cross-generic`; override toolchains with `TOOLCHAIN_PREFIX`/`CROSS` as needed.
- Tests expect deterministic logging via `td_log_writef`; avoid introducing new ERROR-level noise.

## Process & Conventions
- Follow the opt-in `/spec` → `/plan` → `/do` workflow from `AGENTS.md` when requested; specs live in `specs/`, plans in `plans/YYYY-MM-DD-title.md`.
- Use `apply_patch` for updates, keep diffs tight, and stay in ASCII unless the file already contains other encodings.
- Reuse helpers such as `iface_record_select_ip`, `terminal_manager_flush_events`, and `td_log_writef` instead of duplicating logic.
- Keep long-running operations outside critical sections—the manager already builds probe lists under the lock and executes callbacks afterward.
- Logs routed through `td_log_writef` (or adapter sinks) must retain existing phrasing so CLIs/tests continue to parse them.
- Prefer Chinese prose for docs/comments by default, and preserve existing log/CLI strings verbatim.
