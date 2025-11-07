# Stage 4 可观测性与配置笔记

## 文档范围
- 记录阶段 4 的实现要点：运行时配置衔接、日志强化与指标准入。
- 说明新增的 `terminal_manager_stats` 统计口径，以及外部如何读取快照。
- 补充本阶段的验证动作，方便后续阶段在同一基线上继续扩展。

## 运行时配置桥接
- `td_runtime_config` 新增 `keepalive_interval_sec`、`keepalive_miss_threshold`、`iface_invalid_holdoff_sec`、`max_terminals`、`stats_log_interval_sec` 与 `log_level` 字段，`td_config_load_defaults` 输出 Realtek Demo 默认值（120s/3 次/1800s/1000/0s/INFO）。
- `stats_log_interval_sec` 支持通过 `--stats-interval` CLI 参数动态调整，默认 `0`（仅按需打印），可设置为 >0 启用周期性统计日志。
- `td_config_to_manager_config` 负责将上述字段映射到 `terminal_manager_config`，缺省值依旧在 `terminal_manager_create` 内兜底，避免旧调用栈遗漏配置时出现 0 值。
- 当前仍沿用 `scan_interval_ms` 默认值，后续若需要从配置文件调优扫描节奏，仅需在 `td_runtime_config` 补充同名字段并透传即可。

## 日志强化
- 当 `max_terminals` 达到上限时，会输出 `terminal_manager` 组件的 WARN 级日志，携带当前数量与被丢弃终端的 MAC/IP，便于观察容量策略触发频次。
- 主程序新增 `terminal_stats` 组件日志：支持 `kill -USR1 <pid>` 立即打印一次 `terminal_manager_get_stats` 快照，若配置 `stats_log_interval_sec > 0` 则按节奏自动输出，并在收到退出信号时再打印一次终态，方便在线排障。
- 新终端分配失败（内存不足）统一记录为 ERROR 日志，快速暴露资源耗尽风险。
- 默认日志 sink（未注入自定义回调时）会在消息前追加 `YYYY-MM-DD HH:MM:SS` 的系统时间戳，便于与外部日志或主机时间同步排查。
- 虚接口前缀变化、探测失败超阈值等仍沿用 INFO/DEBUG 级别的结构化日志，结合统计指标可还原关键时间线。

## 指标口径
`struct terminal_manager_stats` 现包含以下指标：

| 字段 | 含义 | 更新时机 |
| --- | --- | --- |
| `terminals_discovered` | 成功建表的终端累计数 | `terminal_manager_on_packet` 新建条目 |
| `terminals_removed` | 被引擎移除的终端累计数 | 定时扫描删除条目 |
| `capacity_drops` | 达到容量上限被拒绝的终端数 | 新终端创建前触发容量检查 |
| `probes_scheduled` | 已安排的保活探测次数 | 定时扫描生成 `probe_task` |
| `probe_failures` | 因探测失败被淘汰的终端数 | 超过阈值后删除条目 |
| `address_update_events` | 虚接口 IPv4 前缀增删次数 | `terminal_manager_on_address_update` |
| `events_dispatched` | 成功下发给北向回调的事件条目数 | `terminal_manager_maybe_dispatch_events` |
| `event_dispatch_failures` | 事件批次因内存不足或回调缺失被丢弃次数（包括关闭回调时的残留队列） | `terminal_manager_maybe_dispatch_events` / `terminal_manager_set_event_sink` |
| `current_terminals` | 当前终端表内条目数量 | 新建/删除条目、或通过 `terminal_manager_get_stats` 读取时同步 |

- `terminal_manager_get_stats` 提供线程安全的快照接口，持有管理器互斥锁后拷贝统计结构体，调用方只需传入预分配的 `struct terminal_manager_stats`。
- 统计字段全部在持有 `terminal_manager.lock` 时更新，避免与报文/定时线程互相踩踏；读取时同样在持锁状态下完成拷贝。
- 与时间相关的指标（如保活间隔、接口 holdoff）全部依赖单调时钟采样，确保系统时间调整不会影响统计口径。

## 验证
- 通过 `make cross-generic`（`src/` 目录）使用 `mips-linux-gnu-` 前缀编译，确认新增逻辑不会破坏 MIPS 交叉构建。
- 新增日志和指标不改变既有 API 契约：事件分发与查询仍保持 Stage 3 定义的行为。

## 后续展望
- 统计结构体可作为 CLI/北向调试接口的直接数据源，下一阶段可以在桥接层暴露 JSON 或 CLI 命令以便实时查询。
- 若需要更细粒度的探测耗时或事件延迟，可在现有结构上追加字段，同时保持 `terminal_manager_get_stats` 的向后兼容性。
