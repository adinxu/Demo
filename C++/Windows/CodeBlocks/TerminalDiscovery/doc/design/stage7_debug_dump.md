# 阶段 7：调试导出接口

本阶段向终端发现代理补齐可读的运行态快照能力，允许在不中断业务线程的前提下导出终端哈希桶、接口前缀/绑定索引以及 MAC 查表队列状态。以下内容总结新增数据结构、调用方式、北向封装与测试覆盖，便于集成与验收团队查阅。

## 目标与范围
- C 侧在持锁遍历期间通过统一的 `td_debug_writer_t` 回调输出文本，支持状态、VLAN、ifindex 与 MAC 前缀过滤，并统计已写行数。
- 提供可复用的 `td_debug_dump_opts_t`/`td_debug_dump_context_t`，减少跨多次导出时的重复配置。
- C++ 北向新增 `TerminalDebugSnapshot`，面向 `std::string` 返回调试文本，屏蔽回调细节。
- 单测覆盖过滤、错误回调、空数据集与队列统计，文档提供常见排障场景与示例输出。

## 核心数据结构

| 结构 | 说明 |
| ---- | ---- |
| `td_debug_dump_opts_t` | 控制导出行为的过滤器（状态/VLAN/ifindex/MAC 前缀）及扩展参数（`verbose_metrics`、`expand_terminals`）。|
| `td_debug_dump_context_t` | 记录 `lines_emitted`、`had_error` 与当前 opts 指针，便于调用方在多次导出间复用状态。|
| `td_debug_writer_t` | 回调签名为 `void (*)(void *ctx, const char *line)`，所有导出函数以该回调为唯一输出通道。|
| `td_debug_file_writer_ctx` | 适配 `FILE*` 的默认 writer 包装，自动补充换行并透传错误。|

## C 语言接口

- 导出函数位于 `terminal_manager.c`：
  - `td_debug_dump_terminal_table`
  - `td_debug_dump_iface_prefix_table`
  - `td_debug_dump_iface_binding_table`
  - `td_debug_dump_pending_vlan_table`
  - `td_debug_dump_mac_lookup_queue`
  - `td_debug_dump_mac_locator_state`
- 调用步骤：初始化 `td_debug_dump_context_t`，按需配置 `td_debug_dump_opts_t`，再组织 writer。以下示例写入 `stdout`：

```c
struct td_debug_file_writer_ctx file_ctx;
td_debug_dump_context_t ctx;
td_debug_dump_opts_t opts = {0};

FILE *fp = stdout;
td_debug_context_reset(&ctx, &opts);
td_debug_file_writer_ctx_init(&file_ctx, fp, &ctx);

int rc = td_debug_dump_terminal_table(manager,
                                      &opts,
                                      td_debug_writer_file,
                                      &file_ctx,
                                      &ctx);
```

- 过滤参数说明：
  - `filter_by_state` / `state`：仅输出指定状态终端。
  - `filter_by_vlan` / `vlan_id`：限定 VLAN。
  - `filter_by_ifindex` / `ifindex`：限定逻辑端口。
  - `filter_by_mac_prefix` / `mac_prefix` + `mac_prefix_len`：按字节前缀筛选 MAC。
  - `verbose_metrics`：附加探测失败计数、桥接版本等详细指标。
  - `expand_terminals`：在绑定表导出时展开桶内全部终端。
  - `expand_pending_vlans`：在 pending VLAN 导出时逐项展开挂起终端详情。
- `td_debug_dump_pending_vlan_table` 会先输出整体统计（匹配的桶数与挂起终端数量），随后为每个匹配的 VLAN 打印 `pending vlan=<vid> entries=<count> total=<total>` 摘要；开启 `expand_pending_vlans` 后，将附加 `MAC/IP/状态/pending_vlan_id/meta_vlan/ifindex` 逐行输出，便于定位具体终端。过滤条件与其他导出函数保持一致，可按 VLAN、状态、ifindex 或 MAC 前缀快速聚焦。
- `td_debug_dump_context_t` 可监控是否出现 writer 异常；若 `ctx.had_error == true`，上层应视为导出未完成并记录日志。

## C++ 北向封装

`TerminalDebugSnapshot` 与 `TdDebugDumpOptions` 定义在 `terminal_discovery_api.hpp`，为北向守护进程提供字符串形态的快照：

```cpp
TerminalDebugSnapshot snapshot(manager);
TdDebugDumpOptions options;
options.verboseMetrics = true;
options.expandTerminals = true;

std::string terminals = snapshot.dumpTerminalTable(options);
std::string bindings  = snapshot.dumpIfaceBindingTable(options);
TdDebugDumpOptions pending;
pending.expandPendingVlans = true;
std::string pendings  = snapshot.dumpPendingVlanTable(pending);
std::string prefixes  = snapshot.dumpIfacePrefixTable();
std::string queues    = snapshot.dumpMacLookupQueues();
std::string locator   = snapshot.dumpMacLocatorState();
```

- `TdDebugDumpOptions::to_c()` 自动构造对应的 `td_debug_dump_opts_t`。
- 任何导出失败都会触发 `TD_LOG_WARN` 级别日志，并返回已经积累的部分文本（若存在）。

## 调试建议
- 先导出 `td_debug_dump_terminal_table` 确认哈希桶分布，再结合 `ifindex`/`VLAN` 过滤定位问题终端。
- `td_debug_dump_mac_lookup_queue` 与 `td_debug_dump_mac_locator_state` 可观察 `mac_need_refresh_` / `mac_pending_verify_*` 队列长度和 `mac_locator_version`，排查 MAC 查表延迟或失效。
- 若需确认 VLAN 点查行为，可配合启用 `[switch-mac-stub]` 日志或在 demo 环境设置 `TD_SWITCH_MAC_STUB_LOOKUP` 强制命中/未命中：成功的 `lookup_by_vid` 会立即在终端快照中体现新的 ifindex，同时调试导出显示 `mac_locator_version` 未前进但 `mac_view_version` 已更新；如点查返回 `NOT_READY`，日志会提示等待下一轮快照。
- 在锁持有期间执行 writer，确保输出路径不会阻塞；写文件时推荐使用无缓冲管道或预分配缓冲区。

## 输出示例

```text
terminal mac=02:aa:bb:cc:dd:ee ip=203.0.113.20 vlan=310 state=ACTIVE ifindex=105 raw_bucket=12
iface kernel_ifindex=105 address=203.0.113.1/24 prefixes=1 bindings=1
binding kernel_ifindex=105 vlan=310 entries=1
terminal mac=02:aa:bb:cc:dd:ee ip=203.0.113.20 state=ACTIVE probes_failed=0
pending_vlans buckets=1 terminals=1
pending vlan=311 entries=1 total=1
  terminal mac=02:cc:dd:ee:ff:10 ip=203.0.113.30 state=IFACE_INVALID pending_vlan=311 meta_vlan=-1 ifindex=0
mac_lookup queue pending_refresh=1 pending_verify=0 total=1
mac_locator version=42 subscribed=1 last_refresh_ms=500
```

## 测试覆盖
- `tests/terminal_manager_tests.c::test_debug_dump_interfaces`：验证过滤参数、绑定展开、前缀表与 MAC 队列导出的正确性，并模拟 writer 失败路径。
- `tests/terminal_integration_tests.cpp`：通过 `TerminalDebugSnapshot` 校验 C++ 包装行为，覆盖警告日志与部分文本返回逻辑。

## 集成要点
- C 端 API 暴露于 `src/include/terminal_manager.h`，北向包装位于 `src/common/terminal_northbound.cpp` 与 `src/include/terminal_discovery_api.hpp`。
- 文档与示例输出为验收提供统一参考，便于运维比对实际日志；上线前建议脚本化保存 `terminal_table` 与 `mac_lookup_queue` 输出以追踪趋势。
