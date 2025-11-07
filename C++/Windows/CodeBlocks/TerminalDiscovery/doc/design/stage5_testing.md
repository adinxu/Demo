# 阶段 5 测试策略与现状

## 范围
- 覆盖终端状态机的关键分支：探测失败淘汰、接口失效保留、端口元数据变更。
- 验证事件分发链路与查询接口在无适配器依赖环境下的行为。
- 为后续北向联调与实机压力测试提供可复用的本地快速回归入口。

## 新增测试
- `terminal_discovery_tests`
  - `terminal_add_and_event`：模拟地址前缀 + ARP 报文，确认新增事件、查询快照与统计计数器。具体实现：
    1. 构造 `struct terminal_manager_config`，通过 `selector_callback` 固定返回 ifindex=100、VLAN=100。
    2. 将 `terminal_manager_set_event_sink` 回调指向 `capture_callback`，并通过 `apply_address_update` 注册 `192.0.2.0/24` 前缀。
  3. 使用 `build_arp_packet` 拼装带 VLAN=100、ifindex=7 的 ARP 报文，调用 `terminal_manager_on_packet`。
  4. 断言事件捕获数组仅包含一次 `TERMINAL_EVENT_TAG_ADD`，`ifindex==7`，`terminal_manager_query_all` 返回单条记录，并检查 `terminal_manager_get_stats` 的 `terminals_discovered`/`events_dispatched`。
  - `probe_failure_removes_terminal`：缩短保活周期，触发一次探测即删除，校验探测回调与告警统计。具体实现：
    1. keepalive 缩短至 1s、miss 阈值为 1，selector 返回 ifindex=101。
    2. 注入 `198.51.100.0/24` 前缀和初始报文后，调用 `terminal_manager_on_timer` 触发定时逻辑。
    3. 通过 `sleep_ms(1100)` 写入超时，并在第二次 `on_timer` 后验证：探测回调计数==1，事件捕获得到一条 `TERMINAL_EVENT_TAG_DEL`，统计中的 `probes_scheduled`、`probe_failures`、`terminals_removed` 均为 1。
  - `iface_invalid_holdoff`：移除前缀后等待保留期，验证延迟删除与统计。具体实现：
    1. selector 返回 ifindex=102，配置 `iface_invalid_holdoff_sec=1`，注入 `203.0.113.0/24`。
    2. 报文到达后立刻删除该前缀（`is_add=false`），调用一次 `terminal_manager_on_timer`，确认无增量事件，`terminal_manager_query_all` 仍能枚举终端。
    3. 等待 1.1s 再次执行 `on_timer`，验证捕获到 `TERMINAL_EVENT_TAG_DEL`，统计的 `terminals_removed` 递增。
  - `ifindex_change_emits_mod`：连续两次报文变更入口 ifindex，检查增量上报生成 `MOD` 事件。具体实现：
    1. selector 返回 ifindex=103，注入 `10.10.10.0/24`，首帧 ARP 携带入口 ifindex=101，确认初始 `ADD` 事件。
    2. 清空事件记录后再次下发同一终端但入口 ifindex=102 的报文。
  3. 断言捕获一条 `TERMINAL_EVENT_TAG_MOD`（`ifindex==102`），同时验证探测回调未触发与 `events_dispatched` 计数递增。
- `terminal_integration_tests`
  - `test_duplicate_registration`：调用 `setIncrementReport` 后再次注册同一回调，期望返回 `-EALREADY`，验证北向接口重复注册保护逻辑。
  - `test_increment_add_and_get_all`：使用 stub netlink 与 ARP 报文驱动管理器，确认 `inc_report_adapter` 收到 `ADD` 事件，`getAllTerminalInfo` 至少返回一条记录。
  - `test_netlink_removal`：通过 `terminal_manager_on_address_update` 注销前缀并等待 holdoff，验证 `DEL` 事件上报与查询快照清空。
  - `test_stats_tracking`：读取 `terminal_manager_get_stats`，核对发现/删除/地址更新计数随流程更新。
- 运行时默认将日志等级降至 `ERROR`，避免测试输出噪声。

## 使用方法
- 本地运行：
  - `cd src && make test`
  - 产物：`terminal_discovery_tests`、`terminal_integration_tests`。
- 预期输出：两个可执行文件依次运行；前者逐条打印 `[PASS]`/`[FAIL]`，后者输出阶段性断言及最终 `integration tests passed/failed`。

## 后续计划
1. 实机回归脚本化：整理 Realtek 300/1000 终端回归步骤与性能记录。
