# Stage 3 报文解码与事件上报设计

## 范围
- 承接阶段 2 的终端表与保活引擎，实现 **报文解码→终端事件聚合→北向接口输出** 的闭环。
- 提供增量事件批处理、可配置节流窗口以及全量查询接口，支撑外部 C++ 桥接层的 `MAC_IP_INFO` 结构。

## 核心数据结构
- `terminal_snapshot_t`
  - 终端条目的只读快照，携带 `key` (MAC/IP)、`terminal_metadata`、状态、`tx_iface/tx_ifindex`、时间戳与 `failed_probes`。
  - 同时作为查询输出和事件负载的基础单元。
- `terminal_event_node`
  - 事件队列的链表节点，存储 `snapshot`、可选 `previous`（用于修改前镜像）以及 `next` 指针。
- `terminal_event_queue`
  - 管理新增、删除、修改三条队列的头/尾与节点数量，便于批量分发时精确分配数组容量。

## 事件管线
1. **事件入队**
   - `terminal_manager_on_packet`
     - 新条目：创建后立即生成快照并入新增队列。
     - 存量条目：更新前采集旧快照，更新后调用 `queue_modify_event_if_changed` 判定是否入修改队列。
   - `terminal_manager_on_timer`
     - 每次扫描均采集旧快照；若条目被删除，构造最终快照入删除队列；否则根据差异入修改队列。
   - `terminal_manager_on_iface_event`
     - 接口状态变动前采集旧快照，更新后按需入修改队列。
   - 当未配置事件接收器 (`event_cb == NULL`) 时直接跳过入队，避免无意义分配。

2. **批量分发** – `terminal_manager_maybe_dispatch_events`
   - 在上述 API 释放互斥锁后调用，保证回调执行不持有内部锁。
   - 使用 `event_throttle_sec` 与最近一次分发时间控制节流；`0` 表示实时。
   - 一旦需要分发：
     1. 从队列中摘除全部节点，记录数量。
     2. 为新增/删除/修改分别分配定长数组并拷贝快照（修改事件带上 before/after）。
     3. 调用外部 `terminal_event_callback_fn`，随后释放数组。
   - 若分配失败，记录告警并丢弃该批事件，防止队列持续占用。

3. **显式操作**
   - `terminal_manager_set_event_sink`
     - 配置回调地址及节流窗口，允许 0–3600 秒范围，超过上限自动钳制。
     - 禁用回调时会清空队列；启用后立即触发一次强制分发（以避免漏报首批事件）。
   - `terminal_manager_flush_events`
     - 向外暴露的手动刷新入口（例如关闭前或测试场景）。

## 北向查询
- `terminal_manager_query_all`
  - 先在互斥锁内计算条目数量、分配快照数组并填充，再解锁逐个回调用户函数，保证不会持锁执行外部逻辑。
  - 回调返回 `false` 可提前终止遍历。

## 报文解码注意事项
- `td_adapter_packet_view` 仍是唯一的数据输入：
  - VLAN ID 必定存在（取自 `PACKET_AUXDATA`），若缺失入口口名或 ifindex，则保留现状/记为 `-1`，后续由 `resolve_tx_interface` 和外部选择器兜底。
  - `snapshots_equal` 在判定修改时忽略 `last_seen/last_probe/failed_probes`，避免每帧都触发修改事件；状态、VLAN、入口接口与发包接口发生变化才会产生增量。

## 并发与内存安全
- 事件队列的全部修改都在主互斥锁 `lock` 内完成，确保与终端表更新一致。
- 分发阶段在脱锁状态下执行回调并释放节点，避免潜在的回调重入造成死锁。
- 销毁流程在释放终端条目前先清理三条事件队列，防止残留节点泄漏。

## 关键对外接口速览
```c
int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    unsigned int throttle_sec,
                                    terminal_event_callback_fn cb,
                                    void *cb_ctx);

int terminal_manager_query_all(struct terminal_manager *mgr,
                               terminal_query_callback_fn cb,
                               void *cb_ctx);

void terminal_manager_flush_events(struct terminal_manager *mgr);
```
- 事件批次通过 `terminal_event_batch_t` 提供三个指针数组（added/removed/modified），每个元素对应 `terminal_snapshot_t`，外层负责在回调中转换为 `TerminalInfo` 或其他北向结构。

## 未来扩展点
- 修改判定策略可按需调整（例如增加 `failed_probes`、`last_seen` 触发），仅需更新 `snapshots_equal`。
- 事件队列目前使用链表 + 批量拷贝，后续可替换为 ring buffer 以减少分配开销。
- 若需持久化节流状态，可在回调返回值中添加反馈通道，驱动下一次分发时机。
