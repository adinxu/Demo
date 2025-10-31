# 阶段 0 Raw Socket 演示程序

该示例用于验证在 Linux 环境下通过 Raw Socket 捕获 ARP 报文，并在指定 VLAN 虚接口周期发送 ARP 探测的可行性。程序采用 C 语言编写，可在 x86 开发机直接编译运行，若需在 Realtek MIPS 平台上验证，可通过交叉编译获得可执行文件。

## 编译步骤

```bash
cd src/demo
make
# 或交叉编译（示例）
make CC=mipsel-linux-gnu-gcc CFLAGS="-std=c11 -O2"
```

生成的二进制位于 `src/demo/stage0_raw_socket_demo`。

> **注意**：交叉编译时请将 `mipsel-linux-gnu-gcc` 替换为实际使用的工具链前缀，并确保链接 pthread 库。

## 运行示例

```bash
sudo ./stage0_raw_socket_demo \
  --rx-iface eth0 \
  --tx-iface vlan1 \
  --target-ip 192.168.1.1 \
  --interval 120
```

参数说明：

- `--rx-iface`：监听 ARP 报文的物理接口，默认 `eth0`。
- `--tx-iface`：发送 ARP 探测的 VLAN 虚接口，默认 `vlan1`。
- `--target-ip`：周期探测的目标 IPv4 地址，通常为默认网关。
- `--interval`：探测间隔，单位为秒，默认 120。

程序启动后会在标准输出打印收到的 ARP 报文信息，并在标准错误输出提示发送动作。按 `Ctrl+C` 可正常退出。

## 实机验证流程

1. 将交叉编译后的可执行文件拷贝到 Realtek MIPS 目标设备。
2. 确认 `eth0` 和 `vlan1`（或其它指定接口）已正确配置，并具备 Raw Socket 权限。
3. 准备网络测试仪或其它流量发生工具，模拟不少于 300 个终端的 ARP 行为。
4. 以 root 权限运行程序，观察：
   - 是否持续收到 ARP 请求/响应并打印。
   - 周期性 ARP 探测是否按期发送。
   - 终端上线/下线时输出是否及时反映。
5. 记录 CPU/内存占用、丢包率、超时等指标，将结果整理进阶段 0 验证报告。

## 已知限制

- 运行 Raw Socket 需要 root 权限；如需非特权运行，可为可执行文件设置 `cap_net_raw` 能力。
- 若接口未配置 IPv4 地址，程序会跳过 ARP 请求并发出警告。
- 当前仅支持 ARP 报文，后续协议将在核心实现阶段扩展。
