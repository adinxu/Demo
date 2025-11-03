# 终端发现代理实施计划

## 范围与关联规范
- **目标**：依据 `specs/2025-10-31-terminal-discovery.md` 中的最新需求与约束，构建可跨平台移植的终端发现代理，首期聚焦 Realtek 平台，并确保与外部 C++ 北向接口的 ABI 兼容。
- **关联规范**：`specs/2025-10-31-terminal-discovery.md`

## 假设与非目标
- Realtek 平台具备 Raw Socket 能力并允许绑定 VLAN 虚接口（如 `vlan1`），推荐交叉编译前缀为 `mips-rtl83xx-linux-`（如 `mips-rtl83xx-linux-gcc`）；若该工具链暂不可用，可使用通用 MIPS 交叉工具链验证代码可编译性。
- 终端发现逻辑仅依赖入方向 ARP 报文；适配器需在收包侧过滤掉本机发送的 ARP，避免无意义事件，并在内核剥离 VLAN tag 时通过 `PACKET_AUXDATA` 取回原始 VLAN。
- 设备启动阶段已默认为所有二层口启用 ARP Copy-to-CPU ACL，适配器无需额外校验或感知该配置。
- 设备上存在网络测试仪或等效工具，可模拟 ≥300 个终端。
- 暂不考虑软件层面的 ARP 限速策略；若后续平台启用，需要重新评估。
- 外部团队提供的 C++ API（`MAC_IP_INFO` 及相关回调）按约定稳定，且允许我们在构建链中启用 C/C++ 混合编译。
- 项目默认采用 C 语言实现；仅在对接外部 C++ ABI 时引入必要的桥接代码。
- 当前回调/查询要求输出 `mac`、`ip`、`port` 与变更标签四个字段，并以字符串/整型形式携带；未来扩展将另行评估，并需支持 0–3600 秒的节流配置。
- 开发环境为 x86，而目标 Realtek 平台为 MIPS；与硬件相关的测试需在目标平台上手动运行与验证。
- 不在本轮实现 CLI/UI、DHCP/ND 嗅探或与 FIB 的深度集成。

## 分阶段计划

### 阶段 0：Realtek Demo 验证（已完成）
1. ✅ 搭建测试环境：网络测试仪直连交换机，确认 `eth0` 具备 Raw Socket 收发能力，VLAN1 对应虚接口可成功发包。
2. ✅ 开发 `src/demo/stage0_raw_socket_demo.c`：
   - 接收端固定监听 `eth0`，加载 BPF 过滤器并启用 `PACKET_AUXDATA` 恢复 VLAN；收到 ARP 时打印 opcode/VLAN/源目标信息，可选择十六进制转储。
   - 发送端允许指定 `--tx-iface`、源/目的 MAC/IP、间隔、次数；默认绑定 `vlan1`，周期性下发 ARP 请求。
3. ✅ 实机验证（基础）：确认 RX 能恢复 VLAN、忽略本机发送帧；TX 在 `vlan1` 下发 ARP 且保持 100ms 间隔。
4. ⚠️ 待补充：300 终端规模模拟尚未执行，需补充性能指标（CPU/内存、丢包率）、网络测试仪配置步骤及异常日志样例。

### 阶段 1：适配层设计与实现（已完成）
1. ✅ ABI 设计：`src/include/adapter_api.h` 定义错误码、日志级别、报文视图、接口事件、计时器、ARP 请求结构；`src/include/td_adapter_registry.h` + `src/adapter/adapter_registry.c` 注册并解析唯一 Realtek 适配器描述符。
2. ✅ Realtek 适配器：
   - RX：`realtek_start` 时创建 `AF_PACKET` 套接字，附加 BPF、`PACKET_AUXDATA`，在 `rx_thread_main` 中恢复 VLAN、ifindex、MAC 并回调。
   - TX：`realtek_send_arp` 使用 `send_lock` 节流；优先采用请求内 `tx_iface`，缺省回落到配置接口；必要时调用 `SO_BINDTODEVICE`、查询接口 IPv4/MAC，若接口无 IP 则跳过并记录日志。
   - 接口与定时器：基于标准 netlink 订阅接口事件（监听 VLANIF 上下线、IP 变更、flags 改动），定时逻辑采用 Linux 单调时钟相关 API，避免系统时间调整对节奏造成影响。
   - 生命周期：实现 `init/start/stop/shutdown`，确保线程安全关闭；未实现的接口事件/定时器将返回 `UNSUPPORTED` 并记录告警。
3. ✅ 公共组件：
   - `td_config_load_defaults` 提供统一的默认运行配置（适配器名称 `realtek`、`eth0`/`vlan1`、100ms 节流、INFO 日志级别）。
   - `td_log_writef` 提供结构化日志输出与外部注入能力。

### 阶段 2：核心终端引擎
1. ✅ 终端表与状态机：
   - `terminal_entry` 记录 MAC/IP、Ingress/VLAN 元数据、探测节奏（`last_seen/last_probe/failed_probes`）与发包绑定（`tx_iface/tx_ifindex`）。
   - 状态流转：`ACTIVE ↔ PROBING` 基于报文与保活结果切换，接口失效进入 `IFACE_INVALID` 并按 `iface_invalid_holdoff_sec` 保留 30 分钟。
2. ✅ 调度策略：
   - 专用 `terminal_manager_worker` 线程按 `scan_interval_ms` 周期驱动 `terminal_manager_on_timer`，统一处理过期、保活、删除流程。
   - 后续若需要提高规模弹性，再评估时间轮/小根堆方案，目前观测以 1s 节拍满足需求。
3. ✅ 保活执行：
   - `terminal_manager_on_timer` 聚合需要探测的终端，生成 `terminal_probe_request_t` 队列，脱离主锁逐个回调 `probe_cb`。
   - 超过 `keepalive_miss_threshold` 后清理终端并记录日志，避免 livelock。
4. ✅ 接口感知：
   - 报文回调刷新 ingress/VLAN 元数据，并通过 `resolve_tx_interface` 应用选择器、格式模板或入口接口回退；VLAN ID 始终从 `PACKET_AUXDATA` 恢复，但若底层未提供 ingress 接口名或 ifindex，则回落到配置/选择器给出的发包接口。
   - 接口事件 `terminal_manager_on_iface_event` 及时更新 `tx_ifindex`、重置探测计数，保持 VLANIF 变更后仍可恢复。
   - Trunk 口 VLAN permit 仍依赖底层 ACL 保障，若平台策略改变再跟进。
5. ✅ 并发与锁：
   - 哈希桶访问由主互斥保护，探测回调在 worker 锁外执行，杜绝回调 re-entry 死锁。
6. ✅ 文档：`doc/design/stage2_terminal_manager.md` 描述线程模型、接口解析策略与配置参数取值。

### 阶段 3：报文解码与事件上报
1. ✅ 报文解析：
   - `terminal_manager_on_packet` 在持锁前采集快照，刷新 VLAN/接口元数据并据此触发状态切换；缺失入口口名/ifindex 时回退到 VLAN 模板或选择器结果，保证事件始终携带 VLAN 信息。
   - 依赖 ACL 提供的 VLAN tag 判定终端归属；若 VLANIF 缺失则进入 `IFACE_INVALID`，恢复后重新探测。
2. ✅ 事件队列：
   - 使用单一 FIFO 链表收集 `terminal_event_record_t`（MAC/IP/port + ModifyTag），在 `terminal_manager_maybe_dispatch_events` 内按节流窗口批量分发；默认实时，支持 0–3600 秒自定义节流。
   - 分发阶段在脱锁状态下将节点拷贝为连续数组并释放，内存分配失败时记录告警并丢弃该批次，避免回调阻塞核心逻辑。
3. ✅ 北向接口：
   - 新增 `terminal_manager_set_event_sink`、`terminal_manager_query_all`、`terminal_manager_flush_events`，由本项目导出 `getAllTerminalIpInfo`/`setIncrementReportInterval`，外部团队提供非阻塞的 `IncReportCb`。
   - 查询阶段生成 `terminal_event_record_t` 数组后脱锁回调；订阅阶段支持节流重置与首次强制推送，并在桥接层将记录映射为携带 `tag` 字段的 `MAC_IP_INFO` 单向量。
4. ✅ 文档：
   - `doc/design/stage3_event_pipeline.md` 说明事件链路设计、节流策略、关键数据结构与并发模型，便于后续维护与扩展。

### 阶段 4：配置、日志与文档
1. 配置体系：
   - 扩展 `td_config` 支持终端保活间隔、失败阈值、节流窗口、最大终端数量等参数；后续引擎统一从配置文件读取，暂不使用环境变量通道。
2. 日志与指标：
   - 引入核心模块结构化日志标签（如 `terminal_manager`, `event_queue`）。
   - 暴露探测计数、失败数、接口波动等指标，预留对接 Prometheus 或 CLI 的采集口。
3. 文档：
   - 编写阶段 2+ 核心引擎设计说明、API 参考、构建部署指南，覆盖最新 `MAC_IP_INFO`/`TerminalInfo` 字段约束。

### 阶段 5：测试与验收
1. 单元测试：覆盖状态机（含 30 分钟保留）、接口事件恢复、节流窗口逻辑；使用 mock 终端表验证遍历调度正确性。
2. 集成测试：基于模拟适配器重放报文、接口事件；验证保活探测、删除、接口失效恢复、事件聚合。
3. 北向测试：
   - C/C++ 桥接层回调桩，验证异常捕获、字段完整性、节流配置上下界。
   - 全量查询输出与并发访问。
4. 实机/压力验证：
   - 300 终端 Realtek Demo 回归；1k 终端压力测试记录 CPU/内存/丢包。
5. 验收输出：整理测试报告、回滚策略、性能曲线。

## 依赖与风险
- 依赖网络测试仪能稳定模拟大规模 ARP 终端。
- Raw Socket 权限或平台安全策略可能阻止绑定 VLAN 虚接口。
- Trunk 口在部分 VLAN 未建虚接口的报文行为仍待实验确认，可能影响终端保活策略。
- 若后续需要在 `TerminalInfo` 增加字段或调整序列化格式，需提前与系统集成团队确认版本策略并保持 `MAC_IP_INFO` 兼容性。
- 接口事件源（netlink/SDK）若行为差异大，需追加适配层开发。

## 验证策略
- 单元测试：状态机、定时器、事件节流。
- 集成测试：使用 mock 适配器模拟报文与接口事件。
- 实机测试：Realtek 平台 300 终端 demo；若资源允许扩展到 1000。
- 性能监测：CPU、内存、报文速率、探测成功率，覆盖 200/300/500/1000 终端档位。
- 由于平台差异，所有实机验证步骤需在 MIPS 目标环境手动执行，并记录操作过程与结果。

## 审批与下一步
- 当前状态：阶段 3 报文解码与事件上报已交付（2025-11-03 更新），核心实现见 `doc/design/stage3_event_pipeline.md`。
- 下一步：进入阶段 4，完善配置项覆盖、结构化日志/指标以及文档体系，提前规划北向测试与节流策略验证。
