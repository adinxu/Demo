# Stage 2 终端管理器设计笔记

## 文档范围
- 说明阶段 2 交付的核心终端引擎实现，涵盖状态机、调度线程、接口绑定等逻辑。
- 对关键数据结构与函数进行解构，为后续阶段（事件队列、北向 API）提供基础说明。

## 配置入口
`struct terminal_manager_config` 通过 `terminal_manager_create` 传入，当前支持：
- `keepalive_interval_sec`：终端保活周期（默认 120s）。
- `keepalive_miss_threshold`：连续探测失败阈值（默认 3 次）。
- `iface_invalid_holdoff_sec`：接口失效后保留时间（默认 1800s）。
- `scan_interval_ms`：固定周期扫描全部终端的间隔（默认 1000ms）。
- `vlan_iface_format`：根据 VLAN ID 生成三层虚接口名的格式串，默认 `vlan%u`。
- `iface_selector` / `iface_selector_ctx`：可选回调，支持自定义从 `terminal_metadata` 推导发包接口名/ifindex。

未显式配置的字段会在 `terminal_manager_create` 内自动落到默认值，避免调用方遗漏。

## 核心数据结构
- `struct terminal_entry`
  - 按 `struct terminal_key{mac+ip}` 哈希存储于 `table[256]` 链表中。
  - 记录最近一次报文时间 `last_seen`、最近一次保活探测时间 `last_probe` 以及失败计数。
  - `terminal_metadata` 保留 ARP 报文的 VLAN、入口接口名、入口 ifindex，用作后续接口绑定参考。
  - `tx_iface/tx_ifindex` 保存可用于保活探测的三层接口绑定；解析失败时置空并进入 `IFACE_INVALID`。
  - 时间戳字段（`last_seen`/`last_probe`）统一使用 `CLOCK_MONOTONIC` 采集，避免系统时间跳变对状态机造成干扰。

- `struct terminal_manager`
  - 全局互斥锁 `lock` 保护哈希桶及状态更新。
  - 定时线程相关：`worker_thread` + `worker_lock` + `worker_cond`。线程按 `scan_interval_ms` 周期触发全量扫描。
  - `probe_cb`：由外部注入的探测函数，接收 `terminal_probe_request_t` 快照执行 ARP 保活。

- `terminal_probe_request_t`
  - 在定时扫描阶段生成的快照，包含终端 key、入口元数据、当前 `tx_iface/ifindex`、失败计数等。
  - 保持只读语义，回调层若需要更新状态，应通过引擎公开的接口重新写回。

## 主要流程
### 报文学习 `terminal_manager_on_packet`
1. 解析 ARP 报文中的 `arp_sha` 与 `arp_spa` 作为终端 key。
2. 命中已有条目则刷新 `last_seen` 并重置 `failed_probes`；未命中创建新节点。
3. `apply_packet_binding` 更新 `terminal_metadata` 后调用 `resolve_tx_interface`：
   - 优先执行外部自定义 `iface_selector`。
   - 其次按 `vlan_iface_format` 生成 `vlanX` 等名称并用 `if_nametoindex` 解析 ifindex。
   - 若仍失败则回退到入口接口名/ifindex。
4. 根据绑定结果切换状态：成功时进入 `ACTIVE`，失败时置为 `IFACE_INVALID` 并等待后续事件恢复。

### 定时扫描 `terminal_manager_on_timer`
- 由后台线程或外部手动调用。
- 每次扫描遍历所有哈希桶：
  1. `IFACE_INVALID` 且超过 `iface_invalid_holdoff_sec` 的终端被淘汰。
  2. 其他状态若与上次报文间隔超过 `keepalive_interval_sec`，触发一次保活：
     - 无可用接口时转入 `IFACE_INVALID`。
     - 有接口时切换到 `PROBING`，更新 `last_probe/failed_probes`，生成 `probe_task`。
     - 当失败计数达到阈值则标记删除。
- 探测任务在释放全局锁后顺序执行，避免回调长时间占用关键锁。

### 接口事件 `terminal_manager_on_iface_event`
- 接收到 Netlink/SDK 事件后遍历所有终端：
  - `flags_after` 不含 `IFF_UP` 时将终端转入 `IFACE_INVALID`，重置 ifindex。
  - 接口恢复 `UP` 时清零失败计数、重置探测时间，并尝试通过 `if_nametoindex` 刷新 ifindex。

## 并发与线程模型
- 终端表所有读写均受 `lock` 保护，避免报文线程与定时线程产生竞态。
- 定时线程内部在持锁期间仅构建最小化 `probe_task` 链表，随后释放锁执行回调，保证报文学习延迟最小化。
- `worker_cond` 采用基于单调时钟的超时唤醒，避免系统时间回拨导致的提前/延迟扫描。
- 销毁流程先标记 `worker_stop` 并唤醒线程，待其退出后才释放哈希表和互斥量，避免悬挂访问。

## 对外契约
- `terminal_manager_create/destroy`：构造与清理终端管理器，销毁时会同步等待定时线程退出。
- `terminal_manager_on_packet`：供适配器报文回调调用，线程安全。
- `terminal_manager_on_iface_event`：供接口事件订阅回调调用，线程安全。
- `terminal_probe_fn`：由调用方实现，负责根据 `terminal_probe_request_t` 构造 `td_adapter_arp_request` 并发送。

## 后续扩展点
- Stage 3 将在当前基础上新增增量事件队列，与 `terminal_probe_request_t` 共享 `terminal_metadata`。
- 若扫描量级增加，可以 `worker_cond` 唤醒机制为基础替换为时间轮或按过期时间排序的容器。
- 需要跨网段校验、接口元数据缓存等能力时，可复用 `iface_selector` 回调引入额外的拓扑信息。
