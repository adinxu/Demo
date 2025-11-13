# Stage 3 报文解码与事件上报设计

## 范围
- 承接阶段 2 的终端表与保活引擎，实现 **报文解码→终端事件聚合→北向接口输出** 的闭环。
- 提供增量事件批处理（实时分发）及全量查询接口，支撑外部 C++ 桥接层的 `MAC_IP_INFO` 结构与 `void IncReportCb(const MAC_IP_INFO &info)` 回调。
- 输出字段收敛至 MAC/IP/端口 + ModifyTag，并新增 `prev_ifindex` 指示端口变更前的逻辑索引，避免重复维护冗余元数据。

## 核心数据结构
- `terminal_snapshot_t`
  - 终端条目的轻量快照，仅保留 `terminal_key` 与 `terminal_metadata`（含 VLAN 与 ifindex，若暂未解析则字段值为 `0`）。
  - 主要用于对比事件前后的端口归属和在删除时携带终端标识。
- `terminal_event_record_t`
  - 北向暴露的最小事件载荷，由 `{terminal_key, ifindex, prev_ifindex, terminal_event_tag_t}` 组成；`ifindex` 取自 CPU tag 或桥接解析，若暂不可用则为 `0`。
  - `prev_ifindex` 仅在 `MOD` 事件时填入非零值，表示端口切换之前的逻辑索引，其余事件固定填 `0` 以保持结构稳定。
  - `terminal_event_tag_t` 与外部的 ModifyTag 语义一一对应（`DEL/ADD/MOD`）。
- `terminal_event_node`
  - 单个队列节点，持有一条 `terminal_event_record_t` 与 `next` 指针。
- `terminal_event_queue`
  - 统一的先进先出队列（head/tail/size），串联所有事件类型，等待批量分发。

## 事件管线
1. **事件入队**
   - `terminal_manager_on_packet`
      - 新条目：创建后立即入队 `ADD` 事件，ifindex 取自 CPU tag、桥接解析或其他报文元数据；若平台未携带，则保持 `0`。
      - 存量条目：更新前采集快照与 ifindex 快照，若 ifindex 发生变化则入队 `MOD` 事件，同时把变更前的 ifindex 填入 `prev_ifindex`。
   - `terminal_manager_on_timer`
    - 条目过期或探测失败超过阈值时入队 `DEL` 事件。
    - 仍存活的条目仅当端口变化时才入队 `MOD` 事件，并记录旧端口到 `prev_ifindex`。
   - `terminal_manager_on_address_update`
     - 更新地址表并针对受影响终端清空绑定，必要时触发 `IFACE_INVALID` 状态；操作仅调整内部状态，不直接入队事件，除非后续报文导致终端 ifindex 变化或条目被重新建表。
   - 未配置事件接收器 (`event_cb == NULL`) 时 `queue_event` 会直接返回，不分配节点；后续在释放队列或清理回调时会把这些批次记入 `event_dispatch_failures`。

2. **批量分发** – `terminal_manager_maybe_dispatch_events`
   - 在上述 API 释放互斥锁后调用，保证回调执行不持有内部锁。
   - 每次检测到队列非空即摘除整批事件：
     1. 摘除整条链表并记录事件数量。
     2. 分配连续数组，顺序拷贝 `terminal_event_record_t`。
     3. 调用 `terminal_event_callback_fn(const terminal_event_record_t *records, size_t count, void *ctx)`。
   - 若内存分配失败或已无有效回调，会记录 WARN 并丢弃该批事件；对应的 `event_dispatch_failures` 会自增 1，随后仍释放原队列节点，避免长期堆积。
  - `mac_lookup_execute`（由报文与定时路径触发的异步 MAC 查表）在完成批处理后同样调用该函数，以确保查表阶段累计的 `MOD/DEL` 事件不会滞后。

3. **显式操作**
   - `terminal_manager_set_event_sink`
     - 注册或清除增量上报回调；启用时立即触发一次分发，确保历史积压事件第一时间上报。
     - 关闭回调时若仍有待发事件，会在清理队列的同时增加一次 `event_dispatch_failures` 计数，用于提示北向忘记消费的批次。
     - 设置新回调后在锁外调用 `terminal_manager_maybe_dispatch_events`，即时推送当前批次。
   - `terminal_manager_flush_events`
     - 手动刷新入口（例如关闭前或测试时立即输出积压事件）。
     - 内部直接调用 `terminal_manager_maybe_dispatch_events`，保持与后台分发一致的统计口径。

## 北向查询
- `terminal_manager_query_all`
  - 在互斥锁内统计条目数量并构造 `terminal_event_record_t` 数组，各记录的 `tag` 固定为 `ADD` 表示当前快照，并填充最新的 ifindex 视图；若尚未解析，则按 `0` 上报。`prev_ifindex` 始终为 `0`，保持与增量语义一致。
  - 解锁后依次调用 `terminal_query_callback_fn(const terminal_event_record_t *record, void *ctx)`；回调返回 `false` 时提前终止遍历。

## 北向桥接实现
- 全局激活：`terminal_manager_create`/`terminal_manager_destroy` 通过 `bind_active_manager`/`unbind_active_manager` 维护单例指针，`terminal_manager_get_active` 为 C++ 桥接层提供检索入口，避免调用方直接持有内部句柄。
- 查询接口：`getAllTerminalInfo(MAC_IP_INFO &)` 使用 `terminal_manager_query_all` 生成 `terminal_event_record_t` 序列，并映射为带 `ModifyTag` 与 `prev_ifindex` 的 `TerminalInfo`；北向按需决定展示顺序，并可据此在端口变更时执行补偿逻辑。
- 增量回调：`setIncrementReport(IncReportCb)` 在初始化阶段注册一次回调并调用 `terminal_manager_set_event_sink`；重复调用会返回错误码，避免多次注册。
  - 桥接层维护 `g_inc_report_cb` 全局回调指针，使用 `g_inc_report_mutex` 串行化读写保证线程安全。
  - `inc_report_adapter` 在事件分发线程中运行，将 `terminal_event_record_t` 批次转换成单一 `MAC_IP_INFO`（包含 ifindex），并捕获回调抛出的异常以防影响内部逻辑。
  - 若内存分配失败或回调抛异常，会写入结构化日志并保持内部状态不变。

## 报文解码注意事项
- `td_adapter_packet_view` 仍是唯一数据输入：
  - VLAN ID 来自 `PACKET_AUXDATA`；若平台额外携带 CPU tag，则解析出入口 ifindex 用于事件上报。Realtek 平台暂未提供该信息时，默认填 `0` 并仅作为未知入口上报。
  - ifindex 变化是触发 `MOD` 事件的条件，可避免因时间戳等细节更新导致的噪声。
  - 报文学习阶段若 `resolve_tx_interface` 在地址表中找不到匹配前缀，会立即清空绑定并将终端标记为 `IFACE_INVALID`，因此事件侧仍以最近一次成功绑定的端口为准；无绑定时端口上报为 `0`。

## 并发与内存安全
- 所有终端表与事件队列的修改都在主互斥锁 `lock` 内完成，确保状态一致。
- 分发阶段释放内部锁后执行回调并逐个释放节点，避免回调重入造成死锁。
- `mac_lookup_execute` 运行在无锁上下文中执行适配器查表，并在结束时再次触发 `terminal_manager_maybe_dispatch_events`，使查表结果与事件输出保持同一节奏。
- 销毁流程会在释放终端条目前先清理事件队列，防止残留节点泄漏。

## 关键对外接口速览
```c
int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    terminal_event_callback_fn cb,
                                    void *cb_ctx);

int terminal_manager_query_all(struct terminal_manager *mgr,
                               terminal_query_callback_fn cb,
                               void *cb_ctx);

void terminal_manager_flush_events(struct terminal_manager *mgr);
```
- 事件批次通过 `terminal_event_record_t` 连续数组传递给北向；`terminal_query_callback_fn` 复用同一结构，便于直接转换为携带 `tag` 和 `prev_ifindex` 字段的 `TerminalInfo` 并填充单一 `MAC_IP_INFO`。

## 未来扩展点
- 如需额外字段，可在 `terminal_event_record_t` 中增补并保持顺序拷贝逻辑不变。
- 事件队列目前采用链表 + 批量拷贝，后续可替换为有界 ring buffer 以减少分配开销或提供容量上限。
````
