# VLAN Tag Handling During Demo Validation

## 背景
在阶段 0 Raw Socket demo 验证过程中，发现接收的报文缺少 VLAN tag，而发送的报文仍携带 VLAN tag。为确保终端发现模块能正确关联三层接口，需要理解内核在收包时对 VLAN 的处理方式。

## 关键发现
- 内核在 `__netif_receive_skb_core` 中会调用 `skb_vlan_untag()`，将 802.1Q 标签剥离，并把 VLAN ID 存入 `skb->vlan_tci`。
- 对于 AF_PACKET 套接字，`packet_recvmsg()` 在启用了 `PACKET_AUXDATA` 选项时，会把 `struct tpacket_auxdata` 作为辅助数据返回，包括 `tp_vlan_tci`/`tp_vlan_tpid` 等字段。
- 因此，只要在 Raw Socket 上通过 `setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, ...)` 启用该选项，并改用 `recvmsg()` 读取控制消息，就能获取被内核剥离的 VLAN ID。

## 解决方案摘要
1. 在 Raw Socket 初始化阶段调用 `setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &enable, sizeof(enable))`。
2. 使用 `recvmsg()` 而非 `recvfrom()`，解析 `msg_control` 中的 `tpacket_auxdata`，恢复 VLAN ID，并与负载中的 VLAN 头互为补充。
3. 更新阶段 0 demo（`stage0_raw_socket_demo.c`）和文档，使收、发两端对 VLAN 的处理保持一致，并在用户态统一丢弃所有超出 IEEE 802.1Q 合法范围（1–4094）的 VLAN ID。
4. 验证在物理接口（如 `eth0`）上由用户态直接封装 802.1Q 头即可正常发包，无需为每个 VLAN 绑定虚接口；该结论已反馈至规范与实施计划，虚接口发送作为回退策略保留。

该经验已纳入阶段 0 演示程序，后续平台适配层实现可直接复用。