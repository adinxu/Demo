# Terminal Discovery Copilot Instructions

## Scope & Focus
- Work only under `C++/Windows/CodeBlocks/TerminalDiscovery`; sibling projects are legacy samples.
- Shipping sources live in `src/`; treat `src/ref/` and `src/demo/` as read-only vendor/prototype references.
- Code is ANSI C except `src/common/terminal_northbound.cpp` and C++-based integration tests; mirror the file’s language (docs default to Chinese).

## Architecture & Runtime
- Entrypoint `src/main/terminal_main.c` loads defaults via `td_config`, parses CLI (`--adapter`, `--tx-interval`, `--stats-interval`, `--max-terminals`, `--ignore-vlan`, etc.), wires logging, adapters, manager, netlink, and the interactive REPL (`stats`, `dump terminal`, `ignore-vlan add`, `exit`).
- Embedded callers use `terminal_discovery_initialize` (`src/include/terminal_discovery_embed.h`) to bootstrap `app_context` without the CLI; access `terminal_discovery_get_manager()` for direct manager work.
- `src/common/terminal_netlink.c` subscribes to `NETLINK_ROUTE` (RTM_NEWADDR/DELADDR), maintains per-ifindex prefix lists, and drives `terminal_manager_on_address_update`/`resolve_tx_interface`.
- Worker thread in `terminal_manager_worker` (monotonic clock) handles keepalives, pending VLAN retries, MAC refresh queues, and promotes `address_sync_cb` outside the main lock.

## Manager & Events
- `src/common/terminal_manager.c` maintains 256 hash buckets guarded by `mgr->lock`, queues events until `terminal_manager_flush_events`, and keeps stats via `terminal_manager_get_stats`/`terminal_manager_log_stats`.
- Set sinks with `terminal_manager_set_event_sink`; remember to call `terminal_manager_flush_events` in unit tests or CLI handlers before expecting callbacks.
- Terminal state transitions: ACTIVE → PROBING → IFACE_INVALID (keepalive misses + `iface_invalid_holdoff_sec`). VLAN filters respect `terminal_manager_add/remove_ignored_vlan`; invalid IDs reject early.
- Debug dumps (`td_debug_dump_*`) require a `td_debug_dump_context_t`; C++ helpers in `TerminalDebugSnapshot` wrap these for tests.

## Northbound & Adapters
- `src/common/terminal_northbound.cpp` is the only C++ bridge: `setIncrementReport` allows a single callback (returns `-EALREADY` on repeat) and `getAllTerminalInfo` snapshots under the manager lock; failures log via `td_log_writef`.
- Adapter contracts defined in `src/include/adapter_api.h` and registered in `adapter/adapter_registry.c`; keep new adapters thread-safe and push logs through `td_log_writef`.
- `adapter/realtek_adapter.c` opens AF_PACKET, installs an ARP-only BPF filter, reads VLAN IDs from `PACKET_AUXDATA`, and rate-limits TX with `send_lock` + `last_send` vs `tx_interval_ms`.
- Switch MAC simulation lives in `stub/td_switch_mac_stub.c`; tests assert deterministic ASCII layouts and `[switch-mac-stub]` log tokens—do not change these strings.

## Build, Test & Tooling
- Run builds from `src`: `make` ⇒ `terminal_discovery`; `make clean` removes artifacts; `make cross`/`make cross-generic` set `TOOLCHAIN_PREFIX` for Realtek or generic MIPS toolchains.
- `make test` produces and runs `terminal_manager_tests`, `terminal_integration_tests`, `td_switch_mac_stub_tests`, and `terminal_embedded_init_tests`; flakes usually mean missed `terminal_manager_flush_events` or incorrect netlink lifecycles.
- Tests depend on `td_log_writef` output and CLI strings—avoid formatting changes or extra `ERROR` noise.
- Use `apply_patch` for edits; respect any staged user changes and keep diffs minimal.

## Documentation & Style
- Logging is centralized in `common/td_logging.c`; use `td_log_set_sink` in tests and keep log level parsing consistent.
- Default to Chinese prose for docs/comments unless the file is already English; stay in ASCII unless existing text uses wider characters.
- `/spec → /plan → /do` workflow is opt-in but mandatory once a stage is invoked; specs belong in `specs/`, plans in `plans/YYYY-MM-DD-title.md` and must be approved before `/do`.
