# 阶段 0 Raw Socket 演示

该示例重新建立终端发现项目阶段 0 的验证基线，提供一个 C 语言程序，可实现：

- **接收模式**：在物理接口（如 `eth0`）上捕获入方向 ARP 帧，程序会强制加载内建 BPF 过滤器（筛 EtherType 与 `pkttype`）并启用 `PACKET_AUXDATA`，即使内核剥离了 802.1Q 标签也能打印 VLAN 信息；本机发送的 ARP 会被过滤；
- **发送模式**：在 VLAN 接口（如 `vlan1`）上按可配置节奏发送 ARP 请求，用于验证瑞昱平台 100 ms 探测节奏的可行性。

两个模式可以同时开启。

## 编译

```sh
cd src/demo
make
# 交叉编译示例
make CC=mipsel-linux-gnu-gcc CFLAGS="-std=c11 -O2"
```

生成的可执行文件为 `stage0_raw_socket_demo`。运行时需具备 `sudo` 权限或授予二进制 `cap_net_raw` 能力。

## 使用方法

### 接收模式

```sh
sudo ./stage0_raw_socket_demo --rx-iface eth0 [--timeout 60] [--verbose]
```

- `--rx-iface`：指定抓包接口，默认 `eth0`；
- `--timeout`：接口在指定秒数内无数据后退出（默认不退出）；
- `--verbose`：输出每帧前 64 字节的十六进制预览。

### 发送模式

```sh
sudo ./stage0_raw_socket_demo \
  --tx-iface vlan1 \
  --dst-ip 192.168.1.100 \
  --src-ip 192.168.1.1 \
  --src-mac 00:11:22:33:44:55 \
  --dst-mac ff:ff:ff:ff:ff:ff \
  --interval-ms 100 \
  --count 0
```

- `--tx-iface`：指定发送接口；不提供则发送模式保持关闭；
- `--src-mac` / `--src-ip`：覆盖接口默认源 MAC / IP（未提供时自动查询）；
- `--dst-mac` / `--dst-ip`：设置 ARP 目标（MAC 默认广播）；
- `--interval-ms`：探测间隔，默认 100 ms；
- `--count`：发送次数上限（0 表示持续发送直至中断）。

## 验证清单

1. 在开发机编译并运行接收模式，注入携带 VLAN 标签的 ARP 流量，确认日志中包含 VLAN ID；
2. 交叉编译并部署到瑞昱目标设备，验证广播 ARP 无需额外 ACL 配置即可上送；
3. 使用流量发生器模拟 ≥300 个终端，确认接收端持续输出且无丢包，发送端保持 100 ms 节奏；
4. 在操作期间记录 CPU、内存占用，为阶段 0 报告提供数据。

## 已知限制

- 仅支持 Linux，需要 Raw Socket 权限；
- 目前仅解析最外层 802.1Q 标签，更多嵌套标签可通过 `--verbose` 的十六进制输出查看；
- 按阶段 0 目标，仅关注 ARP 协议。
