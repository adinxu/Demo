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
  - `terminal_metadata` 保留 ARP 报文的 VLAN 与 ifindex（可来自 CPU tag 或 MAC 表查詢，缺失时写入 `0` 代表未知），其中 `ifindex` 统一表示整机逻辑接口标识；`vlan_id` 用于在物理口发包时封装 802.1Q 头部。
  - `tx_iface/tx_kernel_ifindex` 仅在需要回退到 VLAN 虚接口的场景下填充；常规情况下置空并依赖 `meta.vlan_id` + 物理口完成发包，解析失败同样置空并进入 `IFACE_INVALID`。
  - 时间戳字段（`last_seen`/`last_probe`）统一使用 `CLOCK_MONOTONIC` 采集，避免系统时间跳变对状态机造成干扰。

- `struct terminal_manager`
  - 全局互斥锁 `lock` 保护哈希桶及状态更新。
  - 定时线程相关：`worker_thread` + `worker_lock` + `worker_cond`。线程按 `scan_interval_ms` 周期触发全量扫描。
  - `probe_cb`：由外部注入的探测函数，接收 `terminal_probe_request_t` 快照执行 ARP 保活。
  - `const td_adapter_ops *adapter_ops` 与 `mac_locator_ops`：构造阶段从适配器描述符读取，`terminal_manager_create` 会完成订阅并保存回调句柄。
  - `mac_need_refresh_head/tail` 与 `mac_pending_verify_head/tail`：内部 `mac_lookup_task` 队列，分别承载等待 MAC 快照刷新与待验证的终端，确保查表过程在解锁状态下串行执行。
  - `mac_locator_version` 与 `mac_locator_subscribed`：记录最近一次成功刷新版本号及当前订阅状态，避免重复注册或错过增量通知。
  - `iface_address_table`：哈希表维护 `kernel_ifindex -> prefix_list`，每项包含网络地址与掩码长度；仅在持有 `lock` 时更新。
  - `iface_binding_index`：反向索引 `kernel_ifindex -> terminal_entry*` 链表，`resolve_tx_interface` 成功后登记，便于地址变化时精准定位受影响终端。
  - `struct terminal_metadata` 新增字段：
    - `uint32_t ifindex`：来自 MAC 表桥接的整机 ifindex，默认 `0` 表示未知。
    - `uint64_t mac_view_version`：记录最近一次成功解析 ifindex 时的 MAC 缓存版本号，便于快速判断是否需要重新查询。
    - 为避免重复排队 MAC 解析，`terminal_entry` 额外维护 `mac_refresh_enqueued/mac_verify_enqueued` 标志位。
  - 其余字段（VLAN、入口时间戳）保持不变。

- `terminal_probe_request_t`
  - 在定时扫描阶段生成的简化快照，携带终端 key、最近一次确认的 VLAN ID、可选的 `tx_iface/kernel_ifindex` 以及探测前状态，供探测回调直接构造 ARP 请求。
  - `tx_iface/kernel_ifindex` 仅在需要回退到虚接口时才会赋值；常规物理口发包仅依赖 VLAN ID 与全局配置即可完成构帧。
  - 保持只读语义，回调层若需要更新状态，应通过引擎公开的接口重新写回。

## 主要流程
### 报文学习 `terminal_manager_on_packet`
1. 解析 ARP 报文中的 `arp_sha` 与 `arp_spa` 作为终端 key。
2. 命中已有条目则刷新 `last_seen` 并重置 `failed_probes`；未命中创建新节点。
4. `apply_packet_binding` 更新 `terminal_metadata` 后调用 `resolve_tx_interface`：
  - 优先执行外部自定义 `iface_selector`，允许平台覆盖默认 VLAN -> 接口的推导规则。
  - 默认路径仍按 `vlan_iface_format` 生成 `vlanX` 等接口名，利用 `if_nametoindex` 解析出 VLANIF 以便查询地址资源；这些信息用于确定源 IP 与可用性，即便最终发包走物理口。
  - 只有当 `iface_address_table` 中存在命中的前缀时，才认为该 VLAN 的地址上下文有效；否则视为不可保活并保留 VLAN ID 以待后续报文复活。
  - 若无法确认可用地址，`resolve_tx_interface` 会清空 `tx_iface/tx_kernel_ifindex`，从反向索引移除该终端，并触发 `set_state(...IFACE_INVALID)`；成功时将条目加入 `iface_binding_index` 并保持/进入 `ACTIVE`，同时保留 `meta.vlan_id` 供物理口发包使用。
5. 当报文绑定成功且终端 `meta.ifindex == 0`、或 `meta.mac_view_version < mac_locator_version` 时，会尝试解析整机 ifindex：
  - 若已有有效版本号（`mac_locator_version > 0`），就在解锁后立即调度一条 `mac_lookup_task`，调用适配器的 `mac_locator_ops->lookup(mac, vlan, &ifindex, &version)`。
  - 若当前尚未获得版本号，则将终端加入 `need_refresh` 队列（设置 `mac_refresh_enqueued`），等待下一次 MAC 刷新回调批量处理。
  - 查询命中时写回 `meta.ifindex` 与最新 `mac_view_version`，若 ifindex 发生变化（例如 MAC 漂移），会入队 `MOD` 事件；查询返回 `TD_ADAPTER_ERR_NOT_READY` 时则重新排队等待刷新。

#### `resolve_tx_interface` 实现细节
1. 记录历史绑定：在尝试解析前，先缓存旧的 `tx_iface/tx_kernel_ifindex`，用于后续比对及必要时的解绑。
2. 多级候选选择：
  - 首先调用可选的 `iface_selector` 回调返回接口名/kernel ifindex；
  - 若未解析成功且 VLAN ID >= 0，则根据 `vlan_iface_format`（默认 `vlan%u`）拼出接口名并通过 `if_nametoindex` 解析；
  - 如果只有接口名，函数会再次调用 `if_nametoindex` 兜底，确保内核 ifindex 有效。
3. 地址校验：解析出 ifindex 后，会到 `iface_records` 中查找对应地址前缀，并通过 `iface_record_select_ip` 挑选与终端 IP 匹配的源地址；失败时认为候选无效并回退。
4. 解绑与回退：
  - 当候选无效或完全无法解析时，会调用 `iface_binding_detach`（若此前存在绑定）并清空 `tx_iface/tx_kernel_ifindex/tx_source_ip`，随后返回 `false` 以便上层转入 `IFACE_INVALID`；
  - 旧绑定与新的候选 ifindex 不一致时，同样先执行解绑以维持反向索引一致性。
5. 建立新绑定：候选合法时，写入 `tx_iface/tx_kernel_ifindex/tx_source_ip` 并调用 `iface_binding_attach` 加入索引；若 attach 失败（内存不足等），会立即回滚到未绑定状态。
6. 返回值：成功解析且绑定完成时返回 `true`，否则返回 `false`，供调用方控制状态机（`apply_packet_binding` 在失败时将终端标记为 `IFACE_INVALID`）。

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
- 若终端处于 `IFACE_INVALID` 且发现 `mac_view_version` 落后于全局版本，或全局版本尚未就绪时 `ifindex` 仍为 0，则相应地调度即时查找任务或重新排入 `need_refresh` 队列，确保即便缺乏新报文也能触发 ifindex 恢复。

### 地址更新 `terminal_manager_on_address_update`
- 通过 Netlink `RTM_NEWADDR/DELADDR`（或平台等效回调）更新 `iface_address_table`：
  - 在持有 `lock` 的前提下增删前缀，并根据 `kernel_ifindex` 查询 `iface_binding_index`。
  - 对所有关联终端重新校验其 IP 是否仍命中该接口前缀；若不命中则清空回退接口绑定并调用 `set_state(...IFACE_INVALID)`，同时保留 VLAN ID 以便后续报文复活；更新仅影响内部状态，不直接排队事件。
  - 若前缀恢复，只需等待后续报文或定时线程再次调度 `resolve_tx_interface` 即可重新建立绑定；只有当新的报文导致 ifindex 发生变化或终端被重新创建时才会触发对外事件。
  - 当绑定列表因前缀变更而移除终端时，会同步清理 `iface_binding_index` 中对应节点，确保索引与地址表保持一致。
  - `main/terminal_main.c` 在管理器创建后启动 `terminal_netlink` 监听线程，直接调用该接口完成同步，无需额外的适配层事件桥接。

## ifindex 同步策略
Realtek 适配器提供 `mac_locator_ops->subscribe` 接口；`terminal_manager_create` 成功后会注册 `mac_locator_on_refresh(uint64_t version, void *ctx)`，用于感知 MAC 快照更新或失败。
- `mac_locator_on_refresh` 执行流程：
  1. 在持锁状态下取出 `need_refresh`/`pending_verify` 队列，并遍历全部终端，将 `meta.ifindex == 0` 或 `mac_view_version < version` 的条目补齐到这两个队列中（借助 `mac_refresh_enqueued/mac_verify_enqueued` 标志避免重复排队）。
  2. 解锁后调用 `mac_locator_ops->lookup` 执行批量查询：刷新队列命中即更新 `meta.ifindex` 与 `mac_view_version`，必要时入队 `MOD`；验证队列若发现 ifindex 漂移则同样触发事件并更新索引。
  3. 当回调收到 `version == 0` 时，仅记录 WARN 并保留原有队列，等待下一轮刷新。
- `terminal_manager_on_timer` 在保活扫描阶段也会尝试用 `terminal_mac_locator_lookup` 更新 `IFACE_INVALID` 状态的终端，以便在收不到新报文时也能通过 MAC 表漂移检测恢复 ifindex。
- 所有 `mac_locator` 调用都在释放互斥锁后执行，避免桥接刷新过程阻塞报文线程；查询完成后再重新上锁写回终端结构与事件队列。

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
- Stage 3 将在当前基础上新增增量事件队列，继续消费 `terminal_entry` 中的 VLAN/ifindex 元数据；探测请求不再重复存储该信息。
- 若扫描量级增加，可以 `worker_cond` 唤醒机制为基础替换为时间轮或按过期时间排序的容器。
- 需要跨网段校验、接口元数据缓存等能力时，可复用 `iface_selector` 回调引入额外的拓扑信息。
