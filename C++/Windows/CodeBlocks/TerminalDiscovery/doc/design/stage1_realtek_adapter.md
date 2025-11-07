# Stage 1 Realtek 适配器设计笔记

## 文档范围
- 梳理计划阶段 1 交付的代码：适配器接口契约、日志/配置工具、Realtek 原始套接字适配器骨架。
- 说明在核心终端引擎落地前，如何完成入方向 ARP 抓取与出方向保活探测。

## 主要模块
- `src/include/adapter_api.h`：定义 `td_adapter_ops` 及报文视图、接口信息、计时器、ARP 发送请求等数据结构，是引擎与适配器之间的 ABI。
- `src/adapter/adapter_registry.c`：延迟注册内置适配器并按名称解析，目前仅暴露 Realtek 描述符。
- `src/adapter/realtek_adapter.c/.h`：阶段 1 的 Realtek 实现，负责原始套接字生命周期、收包回调、ARP 发送与日志透传。
- `src/include/td_logging.h` + `src/common/td_logging.c`：进程内轻量日志器，可配置日志级别与输出函数。
- `src/include/td_config.h` + `src/common/td_config.c`：提供统一默认配置装载器，方便核心模块在引入配置文件前使用既定的适配器/RX/TX/节流/日志默认值。
- `src/Makefile`：生成静态库 `libterminal_discovery.a`，打包阶段 1 产物以供后续链接。

## 关键数据结构
- `struct td_adapter_ops`：适配器必须实现的函数表（`init/start/stop`、收包注册、`send_arp`、接口查询、计时器、日志桥接等）。
- `struct td_adapter_packet_view`：向引擎上报报文时携带的元数据（原始帧指针、VLAN ID、整机 ifindex（若 CPU tag 提供）以及源/目的 MAC、时间戳）。Realtek 平台暂无 CPU tag，默认将 ifindex 设置为 `0` 表示未知。
- `struct td_adapter_arp_request`：ARP 保活请求描述，阶段 1 重构后新增 `tx_iface`、`tx_ifindex` 与有效标记，允许外层指定发送所用三层接口。
- `struct td_adapter`：Realtek 私有状态，记录配置的接口、套接字 FD、默认 TX 接口的 MAC/IP 缓存、订阅回调、工作线程句柄以及互斥锁。

## 收包路径
1. `realtek_start` 通过 `configure_rx_socket` 打开收包套接字。
2. `configure_rx_socket` 将 `AF_PACKET` 原始套接字绑定到 `rx_iface`，加载强制 BPF 过滤器，并启用 `PACKET_AUXDATA` 以恢复被硬件剥离的 VLAN 标记。BPF 规则由 `attach_arp_filter` 安装，具体逻辑如下：
  - 首先读取 `PKTTYPE`，仅保留 `PACKET_HOST`/`PACKET_BROADCAST`/`PACKET_MULTICAST` 三类帧，丢弃其他来源（如其他网卡回环）。
  - 随后检查以太网类型：若直接等于 `ETH_P_ARP` 则立即放行；若为 802.1Q/802.1ad 则进入下一步。
  - 对 VLAN 框架，会再次读取内层以太网类型，只有当 Encapsulated EtherType 为 `ETH_P_ARP` 时才放行，否则拒绝。
  - 满足上述任一条件后返回 `0xFFFF` 允许整帧递交用户态；未命中时返回 0 将报文丢弃。
3. `ensure_rx_thread` 在需要收包时启动 `rx_thread_main`。
4. `rx_thread_main` 轮询套接字、构造 `td_adapter_packet_view`、从辅助数据或内层头恢复 VLAN，最后触发注册的回调。

## 发包路径
1. 启动阶段调用 `configure_tx_socket` 创建 ARP 套接字，并以物理接口（默认 `eth0`）缓存 ifindex、MAC、IPv4 作为兜底，确保用户态可在同一套接字上插入 VLAN tag。
2. `realtek_send_arp` 先执行最小间隔节流，再依据探测请求的 VLAN/接口信息构造帧：优先在物理口发送，并在以太头后附加 802.1Q header 写入目标 VLAN；在封装前会将 VLAN ID 归一化为 1–4094 的合法范围，发现非法输入直接报错并跳过发送；仅当平台显式拒绝带 VLAN tag 的物理口发包时，才会根据 `tx_iface_valid` 回退至虚接口重新查询元数据。
3. 发送前若需要回退至虚接口，会调用 `SO_BINDTODEVICE` 绑定指定接口；常规路径直接复用物理口套接字，通过 `sendto` 发出自封装的 VLAN 帧。
4. 无论使用哪种接口，若缺少有效 IPv4 地址（默认或 override），按照规范要求跳过此次保活，保持发现与保活路径一致。

## 线程与同步
- `state_lock`：保护收包订阅注册，确保 RX 线程只启动一次。
- `send_lock`：串行化 ARP 发送，维持节流与每次动态绑定的一致性。
- `atomic_bool running`：协调控制面与工作线程的启动/停止。

## MAC 表桥接与 ifindex 获取方案
- Realtek 适配器在编译期直接链接外部团队交付的 `td_switch_mac_bridge` 模块（见 `src/include/td_switch_mac_bridge.h`），从而复用 demo 中已验证的 `td_switch_mac_get_capacity/td_switch_mac_snapshot` 调用路径。`realtek_init` 首次运行时会调用 `td_switch_mac_get_capacity`，将返回值缓存到 `adapter->mac_capacity`，并一次性 `calloc` 对应数量的 `td_switch_mac_entry_t` 缓冲区；若桥接暂不可用，会以 `TD_ADAPTER_ERR_NOT_READY` 形式回传，调用方可按需重试。
  - 开发环境缺失 `libswitchapp.so` 时启用工程内置的弱符号桩实现（`src/stub/td_switch_mac_stub.c`）。桩在第一次调用时打印提示、返回固定容量 1024，并填充少量示例条目；真实桥接编译进最终镜像后会自动覆盖弱符号，无需修改调用方逻辑。
- 适配器新增内部结构 `struct realtek_mac_cache`：
  - `td_switch_mac_entry_t *entries`：指向上述静态缓冲区，生命周期与适配器一致。
  - `uint32_t capacity`/`uint32_t used`：缓存容量与最近一次快照条数。
  - `uint64_t version`：自增版本号，便于终端管理器判断映射新旧。
  - `struct timespec last_refresh`：最近一次成功刷新时间，采用单调时钟采集。
  - `pthread_rwlock_t lock`：保证快照刷新（写）与查询（读）并发安全。
- `mac_cache_refresh_locked(bool force)` 封装调用 `td_switch_mac_snapshot` 的过程：
 1. 写锁保护下检测快照是否过期（默认 `mac_snapshot_ttl_ms = 30000`，可通过后续配置覆盖，延长至 30s 以匹配 ifindex 更新不要求毫秒级实时性的特性）。
 2. 调用桥接接口填充 `entries` 缓冲区，并更新 `used`、`version` 与 `last_refresh`；若桥接返回条目超过容量会立即记录 ERROR 日志并截断，多余条目被丢弃以维持 ABI 约束。
 3. 将 `entries[0..used)` 解析为轻量映射表 `td_mac_location_t{mac,vlan,ifindex,attr}`，按照 MAC 做 FNV 哈希落入 256 个桶，便于 O(1) 查找；桶元素以链表维护并在刷新前全部回收，避免旧数据残留。
- 适配器对外暴露 `realtek_mac_locator_lookup(const uint8_t mac[ETH_ALEN], uint16_t vlan, struct td_mac_location *out)`，该函数在读锁下查询映射；若快照过期会先释放读锁并触发一次 `mac_cache_refresh_locked(true)`，随后重新读取。查找到匹配 VLAN 的条目则返回真实 ifindex，否则给出 `TD_ADAPTER_ERR_NOT_READY`（桥接未初始化/刷新失败）或 `TD_ADAPTER_ERR_INVALID_ARG`（输入非法）。
- `realtek_start` 启动时会拉起后台线程 `mac_cache_worker`：
  - 工作线程监听条件变量 `mac_refresh_cond`，在显式刷新请求、TTL 到期或连续查询未命中时唤醒。
  - 刷新成功后将 `version` 写入 `adapter->mac_cache_version` 并解锁，再通过回调 `adapter_env.log_fn` 记录一次 DEBUG 日志，包含刷新耗时与条目数量。
  - 若桥接暂不可用或返回错误，线程会指数退避重试，并在 `adapter_env.log_fn` 打印 WARN 以提示上层。
- 为了与 demo 行为保持一致，适配器绝不在快照路径内分配临时缓冲区，所有 `td_switch_mac_entry_t` 复用与容量缓存都在 `realtek_init` 阶段完成；桥接模块内部的 `createSwitch` 亦只在装载时执行一次，并由其自行管理线程安全与引用计数。
- 适配器调用链在遇到桥接不可达、快照失败或查不到指定 MAC 时不会阻塞收包线程：查询函数仅返回错误码，终端管理器可选择保留 `ifindex=0` 并等待下次成功刷新；`mac_cache_worker` 将自动在后台重试刷新，避免在报文路径等待。
## 配置与日志
- `td_config_load_defaults` 输出统一默认配置：适配器名 `realtek`、收包口 `eth0`、发包口默认为物理接口（即将切换至 `eth0`，在旧版本中仍可显式指定 `vlan1` 以兼容）、ARP 节流间隔 100ms、日志级别 INFO。
- `td_log_writef` 提供统一的结构化日志入口，通过 `td_adapter_env` 可注入外部日志管道。

## 构建产物
- `src/Makefile` 输出 `libterminal_discovery.a`。`make` 使用本地编译器，`make cross-generic` 通过 `mips-linux-gnu-` 校验交叉编译链路。

## 集成建议
- 阶段 2/3 代码应：
  - 在调用 `send_arp` 前传入终端管理器记录的 VLAN 元数据，并在后续 API 扩展后将其附带到探测请求；仅当平台需要回退时才填充 `tx_iface/tx_iface_valid` 以指示虚接口路径。
  - 在 `start` 之后立即注册收包回调，确保 RX 线程及时运行。
  - 通过 `td_adapter_env` 设置日志回调，使适配器日志并入系统日志。

## 后续优化方向
- 接口元数据缓存：当前 `query_iface_details` 每次 override 都执行 ioctl，后续可结合链路事件缓存并失效，降低频繁查询成本。
- 接口事件订阅：终端发现流程改由公共模块 `terminal_netlink` 统一订阅 netlink IPv4 地址事件并调用管理器接口；适配器 ABI 不再暴露 `subscribe_iface_events` 回调，后续若需要平台特有事件可通过新增专用模块接入。
- 计时器 API：`td_adapter_ops` 已预留接口，后续可封装 POSIX timer 或平台时钟设施供引擎复用。
