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
- `struct td_adapter_packet_view`：向引擎上报报文时携带的元数据（原始帧指针、VLAN ID、逻辑端口 lport、源/目的 MAC、时间戳）。Realtek 平台暂无 CPU tag，默认将 lport 设置为 `0`。
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
