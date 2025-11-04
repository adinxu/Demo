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
  - `terminal_metadata` 保留 ARP 报文的 VLAN 与可选逻辑端口 `lport`（来自 CPU tag，用于事件上报时标识物理口）。
  - `tx_iface/tx_ifindex` 保存可用于保活探测的三层接口绑定；解析失败时置空并进入 `IFACE_INVALID`。
  - 时间戳字段（`last_seen`/`last_probe`）统一使用 `CLOCK_MONOTONIC` 采集，避免系统时间跳变对状态机造成干扰。

- `struct terminal_manager`
  - 全局互斥锁 `lock` 保护哈希桶及状态更新。
  - 定时线程相关：`worker_thread` + `worker_lock` + `worker_cond`。线程按 `scan_interval_ms` 周期触发全量扫描。
  - `probe_cb`：由外部注入的探测函数，接收 `terminal_probe_request_t` 快照执行 ARP 保活。
  - `iface_address_table`：哈希表维护 `ifindex -> prefix_list`，每项包含网络地址与掩码长度；仅在持有 `lock` 时更新。
  - `iface_binding_index`：反向索引 `ifindex -> terminal_entry*` 链表，`resolve_tx_interface` 成功后登记，便于地址变化时精准定位受影响终端。

- `terminal_probe_request_t`
  - 在定时扫描阶段生成的简化快照，仅携带终端 key、当前 `tx_iface/ifindex` 以及探测前状态，供探测回调直接构造 ARP 请求。
  - 只有当 `tx_ifindex > 0` 时才视为已知可用接口；接口名本身（例如按 VLAN 模板拼接出的 `vlanX`）不会单独启用发包。
  - 保持只读语义，回调层若需要更新状态，应通过引擎公开的接口重新写回。

## 主要流程
### 报文学习 `terminal_manager_on_packet`
1. 解析 ARP 报文中的 `arp_sha` 与 `arp_spa` 作为终端 key。
2. 命中已有条目则刷新 `last_seen` 并重置 `failed_probes`；未命中创建新节点。
3. `apply_packet_binding` 更新 `terminal_metadata` 后调用 `resolve_tx_interface`：
  - 优先执行外部自定义 `iface_selector`。
  - 其次按 `vlan_iface_format` 生成 `vlanX` 等名称并用 `if_nametoindex` 解析 ifindex。
  - 当获得 `ifindex > 0` 时，再检查 `iface_address_table`，确认终端 IP 落入该接口的任一前缀后才视为可用；若接口尚未在地址表中登记任何前缀，同样视为绑定失败。
4. 如果绑定失败，`resolve_tx_interface` 会立即清空 `tx_iface/tx_ifindex`，从反向索引移除该终端，并触发 `set_state(...IFACE_INVALID)`；成功时将条目加入 `iface_binding_index` 并保持/进入 `ACTIVE`。

### 定时扫描 `terminal_manager_on_timer`
- 由后台线程或外部手动调用。
- 每次扫描遍历所有哈希桶：
  1. `IFACE_INVALID` 且超过 `iface_invalid_holdoff_sec` 的终端被淘汰。
  2. 其他状态若与上次报文间隔超过 `keepalive_interval_sec`，触发一次保活：
     - 无可用接口时转入 `IFACE_INVALID`。
     - 有接口时切换到 `PROBING`，更新 `last_probe/failed_probes`，生成 `probe_task`。
     - 当失败计数达到阈值则标记删除。
- 条目被删除前会先从 `iface_binding_index` 移除，避免后续地址事件仍引用已释放终端。
- 探测任务在释放全局锁后顺序执行，避免回调长时间占用关键锁。

### 地址更新 `terminal_manager_on_address_update`
- 通过 Netlink `RTM_NEWADDR/DELADDR`（或平台等效回调）更新 `iface_address_table`：
  - 在持有 `lock` 的前提下增删前缀，并根据 `ifindex` 查询 `iface_binding_index`。
  - 对所有关联终端重新校验其 IP 是否仍命中该接口前缀；若不命中则清空绑定并调用 `set_state(...IFACE_INVALID)`，更新仅影响内部状态，不直接排队事件。
  - 若前缀恢复，只需等待后续报文或定时线程再次调度 `resolve_tx_interface` 即可重新建立绑定；只有当新的报文导致 `lport` 发生变化或终端被重新创建时才会触发对外事件。
  - 当绑定列表因前缀变更而移除终端时，会同步清理 `iface_binding_index` 中对应节点，确保索引与地址表保持一致。
  - `main/terminal_main.c` 在管理器创建后启动 `terminal_netlink` 监听线程，直接调用该接口完成同步，无需额外的适配层事件桥接。

## 并发与线程模型
- 终端表、`iface_address_table` 以及 `iface_binding_index` 的读写均受 `lock` 保护，避免报文线程、地址事件线程与定时线程之间产生竞态。
- 定时线程内部在持锁期间仅构建最小化 `probe_task` 链表，随后释放锁执行回调，保证报文学习延迟最小化。
- `worker_cond` 采用基于单调时钟的超时唤醒，避免系统时间回拨导致的提前/延迟扫描。
- 销毁流程先标记 `worker_stop` 并唤醒线程，待其退出后才释放哈希表、地址表、反向索引和互斥量，避免悬挂访问。

## 对外契约
- `terminal_manager_create/destroy`：构造与清理终端管理器，销毁时会同步等待定时线程退出。
- `terminal_manager_on_packet`：供适配器报文回调调用，线程安全。
- `terminal_manager_on_address_update`：供地址事件订阅回调调用，线程安全。
- `terminal_probe_fn`：由调用方实现，负责根据 `terminal_probe_request_t` 构造 `td_adapter_arp_request` 并发送。

## 后续扩展点
- Stage 3 将在当前基础上新增增量事件队列，继续消费 `terminal_entry` 中的 VLAN/lport 元数据；探测请求不再重复存储该信息。
- 若扫描量级增加，可以 `worker_cond` 唤醒机制为基础替换为时间轮或按过期时间排序的容器。
- 需要跨网段校验、接口元数据缓存等能力时，可复用 `iface_selector` 回调引入额外的拓扑信息。
