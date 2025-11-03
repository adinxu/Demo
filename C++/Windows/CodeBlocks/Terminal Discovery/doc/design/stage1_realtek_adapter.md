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
- `struct td_adapter_packet_view`：向引擎上报报文时携带的元数据（原始帧指针、VLAN ID、入方向接口名与 ifindex、源/目的 MAC、时间戳）。
- `struct td_adapter_arp_request`：ARP 保活请求描述，阶段 1 重构后新增 `tx_iface`、`tx_ifindex` 与有效标记，允许外层指定发送所用三层接口。
- `struct td_adapter`：Realtek 私有状态，记录配置的接口、套接字 FD、默认 TX 接口的 MAC/IP 缓存、订阅回调、工作线程句柄以及互斥锁。

## 收包路径
1. `realtek_start` 通过 `configure_rx_socket` 打开收包套接字。
2. `configure_rx_socket` 将 `AF_PACKET` 原始套接字绑定到 `rx_iface`，加载强制 BPF 过滤器，仅放行入方向 ARP/VLAN 报文，并启用 `PACKET_AUXDATA` 以恢复被硬件剥离的 VLAN 标记。
3. `ensure_rx_thread` 在需要收包时启动 `rx_thread_main`。
4. `rx_thread_main` 轮询套接字、构造 `td_adapter_packet_view`、从辅助数据或内层头恢复 VLAN，最后触发注册的回调。

## 发包路径
1. 启动阶段调用 `configure_tx_socket` 创建 ARP 套接字，并缓存默认 TX 接口的 ifindex、MAC、IPv4 作为兜底。
2. `realtek_send_arp` 先执行最小间隔节流，再解析实际发包接口：若请求携带 `tx_iface_valid` 则为指定接口重新查询元数据，否则使用默认缓存。
3. 发送前调用 `SO_BINDTODEVICE` 将原始套接字绑定到目标接口，构造 ARP 以太帧后通过 `sendto` 发出。
4. 若所选接口缺少 IPv4 地址（默认或 override），按照规范要求跳过此次保活，保持发现与保活路径一致。

## 线程与同步
- `state_lock`：保护收包订阅注册，确保 RX 线程只启动一次。
- `send_lock`：串行化 ARP 发送，维持节流与每次动态绑定的一致性。
- `atomic_bool running`：协调控制面与工作线程的启动/停止。

## 配置与日志
- `td_config_load_defaults` 输出统一默认配置：适配器名 `realtek`、收包口 `eth0`、发包口 `vlan1`、ARP 节流间隔 100ms、日志级别 INFO。
- `td_log_writef` 提供统一的结构化日志入口，通过 `td_adapter_env` 可注入外部日志管道。

## 构建产物
- `src/Makefile` 输出 `libterminal_discovery.a`。`make` 使用本地编译器，`make cross-generic` 通过 `mips-linux-gnu-` 校验交叉编译链路。

## 集成建议
- 阶段 2/3 代码应：
  - 在调用 `send_arp` 前传入终端管理器记录的最近一次发现接口，填充 `td_adapter_arp_request.tx_iface` 与 `tx_iface_valid`。
  - 在 `start` 之后立即注册收包回调，确保 RX 线程及时运行。
  - 通过 `td_adapter_env` 设置日志回调，使适配器日志并入系统日志。

## 后续优化方向
- 接口元数据缓存：当前 `query_iface_details` 每次 override 都执行 ioctl，后续可结合链路事件缓存并失效，降低频繁查询成本。
- 接口事件订阅：Realtek 适配器现阶段返回 `UNSUPPORTED`，阶段 2 可通过 netlink 或芯片 SDK 暴露 VLANIF 状态。
- 计时器 API：`td_adapter_ops` 已预留接口，后续可封装 POSIX timer 或平台时钟设施供引擎复用。
