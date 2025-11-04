# Terminal Discovery 源码速览

本笔记梳理 `src/` 目录的主要组件、核心数据结构、关键函数和线程模型，便于后续团队成员快速理解与维护终端发现代理。

## 模块分层

```
src/
 ├── common/
 │   ├── td_logging.c/.h
 │   ├── td_config.c/.h
 │   ├── terminal_manager.c/.h
 │   └── terminal_northbound.cpp / terminal_discovery_api.hpp
 ├── adapter/
 │   ├── adapter_registry.c/.h
 │   └── realtek_adapter.c/.h
 └── main/
     └── terminal_main.c
```

### 1. 日志子系统 `common/td_logging`
- 提供线程安全的日志级别、输出接口（`td_log_writef`）。
- 默认写 `stderr`，可通过 `td_log_set_sink` 注入外部回调（适配层使用）。
- `td_log_level_from_string` 支持 CLI 级别解析。

### 2. 运行时配置 `common/td_config`
- `td_config_load_defaults` 输出运行所需的基础参数（适配器名、收发接口、保活周期、容量上限等）。
- `td_config_to_manager_config` 将运行时结构体映射为 `terminal_manager` 的内部配置。
- 默认值与 Stage 4 文档保持一致，可通过 CLI 修改（见 `terminal_main.c`）。

### 3. 平台适配层 `adapter/`
- `adapter_registry` 负责按名称查找适配器（目前仅内置 `realtek`）。
- `realtek_adapter`
  - `td_adapter_ops` 实现：`init/start/stop/register_packet_rx/send_arp/...`
  - **线程模型**：
    - 主线程执行 `init/start` 等生命周期回调。
    - `rx_thread_main` 独立线程轮询 AF_PACKET 套接字，解析 VLAN/ARP，并通过注册的回调上送 `td_adapter_packet_view`。
    - 发送路径在 `send_arp` 内部串行化（互斥锁 + 节流）。
  - 所有平台 I/O 均通过原生 Raw Socket 完成，避免依赖平台 SDK。

### 4. 核心引擎 `common/terminal_manager`
- **主要数据结构**：
  - `terminal_manager`
    - 哈希桶 `table[256]` 存储终端条目。
    - 互斥量 `lock` 保护终端表，`worker_thread` 驱动定期扫描。
    - 事件队列 `terminal_event_queue` 存放北向变更通知。
    - 统计字段 `terminal_manager_stats`（Stage 4 新增）。
  - `terminal_entry`
    - 记录 MAC/IP、状态机（`terminal_state_t`）、最近报文时间、探测信息和接口元数据。
  - `terminal_event_record_t`
    - 用于增量事件（`ADD/DEL/MOD`），供北向转换为 `TerminalInfo`。
- **关键函数**：
  - `terminal_manager_create/destroy`：初始化线程、绑定全局单例（`terminal_manager_get_active`）。
  - `terminal_manager_on_packet`：处理适配器上送的 ARP 数据；若是新终端则执行业务校验、入表、触发事件。
  - `terminal_manager_on_timer`：由后台线程调用，负责保活探测、过期清理与队列出列；通过回调 `terminal_probe_fn` 执行 ARP 请求。
  - `terminal_manager_on_iface_event`：接口状态更新（目前适配层未实现，上层预留）。
  - `terminal_manager_maybe_dispatch_events`：批量投递事件到北向回调。
  - `terminal_manager_get_stats`：返回当前计数器快照。
- **线程模型**：
  - 后台 `worker_thread` 每 `scan_interval_ms` 唤醒执行 `terminal_manager_on_timer`。
  - 适配器 RX 线程在收到报文后调用 `terminal_manager_on_packet`（持 `lock`）。
  - 北向事件分发在脱锁后执行，避免长时间占用互斥量。

### 5. 北向桥接 `common/terminal_northbound.cpp`
- 向外导出稳定 ABI：`getAllTerminalInfo`、`setIncrementReport`。
- `setIncrementReport`：注册 C++ 回调 `IncReportCb`，内部通过 `terminal_manager_set_event_sink` 绑定事件入口。
- `getAllTerminalInfo`：调用 `terminal_manager_query_all` 生成快照，转换为 `MAC_IP_INFO`。
- 拥有独立互斥锁 `g_inc_report_mutex` 保证回调注册的线程安全。
- **依赖**：使用 `terminal_manager_get_active` 获取全局管理器指针（由 `terminal_manager_create` 绑定）。

### 6. 主程序 `main/terminal_main.c`
- 负责将上述组件组合成终端发现进程：
  1. `td_config_load_defaults` -> 解析 CLI 参数 -> 设置日志级别。
  2. 查找适配器 (`td_adapter_registry_find`)，初始化 `td_adapter_ops`。
  3. 创建 `terminal_manager`，注册事件回调 `terminal_event_logger`。
  4. 将适配器报文回调绑定至 `terminal_manager_on_packet`。
  5. 启动适配器并进入主循环（等待信号退出）。
  6. 收到 SIGINT/SIGTERM 后依次停止适配器、销毁管理器、输出 shutdown 日志。
- `terminal_probe_handler`：实现 `terminal_probe_fn`，将探测请求翻译成 `td_adapter_arp_request` 调用 `send_arp`。
- `terminal_event_logger`：默认注册的事件回调，将终端的 `ADD/DEL/MOD` 变更写入结构化日志，便于观察流水线行为或在没有北向监听器时进行测试验证。
- CLI 支持配置适配器名、接口、保活参数、容量阈值、日志级别等。
- 通过 `adapter_log_bridge` 将适配器内部日志回落至 `td_logging`。

## 多线程与同步

| 线程 | 来源 | 主要职责 | 同步方式 |
| ---- | ---- | -------- | -------- |
| 主线程 | `main()` | CLI 解析、初始化、信号监听、最终清理 | 使用信号处理器设置 `g_should_stop` 原子变量 |
| 适配器 RX 线程 | `realtek_adapter` | `poll` + `recvmsg` 收取 ARP，并调用 `terminal_manager_on_packet` | 访问终端表时依赖 `terminal_manager` 的 `lock` |
| 终端管理器 Worker | `terminal_manager_worker` | 定期扫描终端表、安排探测、淘汰终端 | `worker_lock` 控制线程休眠，核心操作持 `lock` |
| 北向回调（可选） | `terminal_manager_maybe_dispatch_events` | 在脱锁环境调用外部回调 | 事件队列在 `lock` 下构建；回调执行期间不持锁 |

互斥和条件变量主要来源：
- `terminal_manager.lock`：保护终端哈希表、事件队列、统计数据。
- `terminal_manager.worker_lock/cond`：唤醒/停止后台线程。
- `realtek_adapter.state_lock`：保护数据流回调注册。
- `realtek_adapter.send_lock`：串行化 ARP 发送、实现节流。
- `g_inc_report_mutex`：控制北向回调注册。

## 关键数据流

1. **发现链路**：
   - Realtek 适配器 (`rx_thread_main`) -> `terminal_manager_on_packet` -> 更新终端状态 -> 入队事件 -> `terminal_manager_maybe_dispatch_events` -> 北向回调/日志。
2. **保活链路**：
   - Worker 线程 (`terminal_manager_on_timer`) -> 决定是否探测 -> `terminal_probe_handler` -> `realtek_adapter.send_arp` -> 网络。
3. **查询接口**：
  - 北向 `getAllTerminalInfo` -> `terminal_manager_query_all` -> C++ 向量结果。
4. **配置入口**：
   - CLI -> `td_runtime_config` -> `td_config_to_manager_config` -> `terminal_manager_create`。

## 维护建议
- 修改 `terminal_manager` 状态机或事件逻辑时，同时更新 `doc/design/stage3_event_pipeline.md` 与相关测试计划。
- 引入新平台适配器需要补充 `adapter_registry` 并实现 `td_adapter_ops` 契约。
- 若扩展 CLI 参数，可在 `terminal_main.c` 增加 `getopt_long` 分支，并同步 Stage 4/overview 文档。
- 修改统计字段时，务必保证在持 `lock` 状态下更新/读取以保持线程安全。

## 参考文档
- `doc/design/stage2_terminal_manager.md`: 终端管理器状态机与调度细节。
- `doc/design/stage3_event_pipeline.md`: 事件队列与北向接口设计。
- `doc/design/stage4_observability.md`: 指标、日志与配置增强说明。

本文档可与上述阶段性设计稿配合使用，帮助新成员快速定位代码入口与线程交互方式。
