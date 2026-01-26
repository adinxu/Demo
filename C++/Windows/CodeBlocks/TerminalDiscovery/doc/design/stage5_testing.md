# 阶段 5 测试策略与现状

## 范围
- 构建可在本地容器运行的端到端测试矩阵，覆盖终端状态机、事件分发、北向接口与 MAC 桥接桩。
- 维持 `make test` 单条命令即可完成快速回归，便于迭代时验证核心行为未回归。

## 测试产物

| 可执行文件 | 源文件 | 主要覆盖范围 |
| --- | --- | --- |
| `terminal_discovery_tests` | `tests/terminal_manager_tests.c` | C 侧单元测试（状态机、事件、日志） |
| `terminal_integration_tests` | `tests/terminal_integration_tests.cpp` | C++ 北向 ABI、增量/全量接口、统计口径 |
| `realtek_mac_stub_tests` | `tests/realtek_mac_stub_tests.c` | Realtek MAC 桥接桩的容量/快照/参数校验 |
| `terminal_embedded_init_tests` | `tests/terminal_embedded_init_tests.c` | 嵌入式入口初始化/清理流程（适配器注册表、netlink、北向回调桩） |

## 单元测试：`terminal_discovery_tests`

- `default_log_timestamp`：重定向 `stderr` 验证默认日志 sink 是否追加 `YYYY-MM-DD HH:MM:SS` 时间戳。
- `terminal_add_and_event`：构造 VLAN 100 的终端，校验 `ADD` 事件、查询快照与统计计数，同时确认 `prev_ifindex` 在新增场景恒为 0。
- `probe_failure_removes_terminal`：1 秒保活 + 1 次失败阈值，确认探测回调、`DEL` 事件以及 `probes_scheduled/probe_failures/terminals_removed` 统计。
- `iface_invalid_holdoff`：移除地址前缀触发保留期，验证 holdoff 期间终端仍可查询，超时后才产生 `DEL` 事件。
- `ifindex_change_emits_mod`：同一终端入口 ifindex 变化触发 `MOD` 事件，验证 `prev_ifindex` 返回旧端口索引，并确保探测回调未误触发。

所有测试均通过桩选择器返回固定 ifindex/VLAN，避免依赖真实适配器；日志级别强制降为 `ERROR`，确保输出干净可读。

## 集成测试：`terminal_integration_tests`

- `test_duplicate_registration`：再次调用 `setIncrementReport` 期望 `-EALREADY`，验证北向重复注册保护。
- `test_increment_add_and_get_all`：模拟地址事件 + ARP 报文，确认增量批次仅含 `ADD` 事件，`getAllTerminalInfo` 返回一致的 ifindex/prev_ifindex 组合（新增场景仍为 0）。
- `test_netlink_removal`：删除前缀并等待 holdoff，检查 `DEL` 事件与全量快照清空。
- `test_cross_vlan_migration`：构造「先在缺失 VLANIF 的 VLAN 被学习 → 地址表补齐 → 再迁移到另一 VLAN」的序列，验证 `pending_vlans` 桶如何在 `RTM_NEWADDR` 事件后驱动终端出队、`MOD` 事件携带新旧 ifindex，以及 debug dump 中 `tx_kernel_ifindex/tx_src` 的即时变化。
- `test_ipv4_recovery`：模拟 VLANIF IPv4 删除并恢复，确认终端进入 `IFACE_INVALID` 后仍保留在 `pending_vlans` 中，地址恢复时无需额外报文即可重新绑定，并检查 debug dump 与最终 `DEL` 事件。
- `test_stats_tracking`：在上述流程收尾阶段读取统计，核对发现/移除/地址事件总数是否包含跨 VLAN 迁移与 IPv4 恢复场景。

测试过程同样关闭日志噪声，并通过全局捕获器记录增量批次；最终输出 `integration tests passed/failed` 便于自动化脚本判定结果。

## 桩验证：`realtek_mac_stub_tests`

- `test_invalid_arguments`：传入空指针，确认桩实现返回 `-EINVAL`。
- `test_get_capacity`：校验容量查询返回值 ≥1。
- `test_snapshot_defaults`：默认条目数量大于 0，并打印样例行数。
- `test_snapshot_with_limit`：通过环境变量 `TD_SWITCH_MAC_STUB_COUNT` 约束返回条目数，验证截断逻辑。

这些测试确保弱符号桩满足适配器预期：调用方需先通过 `get_capacity` 预分配缓冲区，`snapshot` 使用输出参数回传实际条目数。

## 嵌入式初始化：`terminal_embedded_init_tests`

- 验证 `terminal_discovery_initialize/cleanup` 在适配器初始化失败、管理器创建失败、netlink 启动失败、北向封装失败等场景下的清理路径（停止适配器、销毁管理器、重置事件 sink、刷新计数器）。
- 覆盖成功路径下的单例访问器与事件回调注册，确认初始化后 `terminal_discovery_get_manager()`/`terminal_discovery_get_app_context()` 可用，清理后被复位。

## 运行方式

```sh
cd build/realtek
make test
```

命令会顺序执行四组测试；如任一失败，将直接以非零退出码告知调用方。`make clean` 可清理生成的目标文件与测试二进制。

## 后续工作

1. 补充针对 MAC 定位失败/重试的桩测试，验证 `mac_need_refresh`/`mac_pending_verify` 队列的恢复逻辑。
2. 在具备真实 Realtek 平台时，引入脚本化的 300/1000 终端压力回归，并将关键指标（CPU/内存/探测成功率）纳入文档。
