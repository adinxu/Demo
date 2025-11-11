# 终端发现代理设备级测试用例

## 文档关联
- 规范：`specs/2025-10-31-terminal-discovery.md`
- 计划：`plans/2025-10-31-terminal-discovery.md`
- 设计参考：`doc/design/stage6_embedded_init.md`、`doc/design/src_overview.md`

## 测试环境准备
- 目标设备：Realtek 平台交换机（支持 Raw Socket 与 802.1Q 直出），固件启用 ARP copy-to-CPU ACL。
- 宿主进程：按计划嵌入方式编译，链接 `terminal_discovery` 全量模块并定义 `TD_DISABLE_APP_MAIN`。
- 网络拓扑：
  - 至少 1 台流量发生器/终端模拟器，可模拟 ≥1000 个 ARP 客户端并控制 VLAN、MAC、IP、发包速率。
  - 管理侧服务器用于收集日志、回调结果与性能指标，支持 SSH 登录设备。
- 工具依赖：
  - 交叉编译产物部署于 `/usr/bin/terminal_discovery` 或宿主自定义路径。
  - `tcpdump` 或等效抓包工具，支持 VLAN 报文解码。
  - 自研或第三方 MAC 表查询脚本，便于验证 `td_switch_mac_snapshot` 返回值。
  - 设备时钟同步（NTP），确保日志时间戳与外部工具可比对。
- 配置基线：
  - `td_runtime_config` 采用默认值，除非用例特意覆盖。
  - 启动前清空终端表，确认无残留管理线程。

## 用例分组

### A. 初始化与回滚
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| A01 | 验证嵌入式初始化成功路径 | 设备空跑，无既存实例 | 1) 宿主进程调用 `terminal_discovery_initialize(NULL)` 2) 观察日志与返回值 | 返回 0；日志包含 `starting (adapter=...)`；`terminal_discovery_get_manager()`/`_get_app_context()` 返回非空 |
| A02 | 验证重复初始化保护 | A01 已执行且未清理 | 再次调用 `terminal_discovery_initialize(NULL)` | 返回 `-EALREADY`；日志输出告警但不破坏已运行实例 |
| A03 | 验证失败回滚 | 将 `td_runtime_config.adapter_name` 配置为不存在的值 | 调用初始化入口 | 返回 `-ENOENT`；日志包含 `adapter 'xxx' not found`；无线程残留，`terminal_discovery_get_manager()` 为 NULL |
| A04 | 验证 `terminal_discovery_bootstrap` 清理 | 模拟 `td_config_to_manager_config` 返回错误（例如临时修改配置触发边界） | 启动宿主并注入失败分支 | 返回非 0；netlink、适配器、事件 sink 均未注册，日志确认 `terminal_discovery_cleanup` 顺序完整 |

### B. 默认日志 sink
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| B01 | 验证默认日志挂载 | 初始化成功且未调用 `setIncrementReport` | 触发终端新增/删除（通过终端模拟器发送 ARP） | 日志输出 `event=<TAG> mac=<MAC> ip=<IP> ifindex=<IDX>`，标签匹配 ADD/DEL/MOD |
| B02 | 验证重复挂载保护 | 在默认模式下通过 C++ 桥接再次调用 `terminal_northbound_attach_default_sink` | 观察返回值与日志 | 返回 `-EALREADY`，日志提示已存在回调，事件输出不受影响 |

### C. 北向回调切换
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| C01 | 首次注册增量回调 | 默认日志模式运行中 | 调用 `setIncrementReport(cb)`，cb 将批次写入文件 | 返回 0；立即收到当前终端快照；后续事件仅通过回调输出 |
| C02 | 重复注册保护 | C01 已注册 | 再次调用 `setIncrementReport(cb2)` | 返回 `-EALREADY`；原回调仍生效；日志写入警告 |
| C03 | 回调异常保护 | 在回调中故意抛异常或阻塞 2s | 观察模块行为 | 日志记录异常并输出空批次告警；事件队列不中断 |
| C04 | 回退至日志模式 | 运行中解除回调（调用 `setIncrementReport(NULL)` 不支持，需重启） | 重启后不注册回调 | 日志恢复默认输出；行为与 B01 一致 |

### D. 终端生命周期
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| D01 | ACTIVE → PROBING 流程 | keepalive 配置默认 (120s/3 次) | 1) 新增终端 2) 停止其响应 | 日志/回调先后出现 `MOD tag=MOD`（状态 PROBING），三次探测失败后输出 `DEL` |
| D02 | IFACE_INVALID 处理 | 移除终端绑定 VLANIF 的 IPv4 地址 | 观察事件输出 | 输出 `MOD` 到 `IFACE_INVALID`；保活暂停；恢复地址后重新探测 |
| D03 | Holdoff 删除 | 按配置观测 30 分钟无恢复 | 终端最终被删除；`terminal_manager_stats.capacity_drops` 不增加 |
| D04 | MAC 表刷新 | 终端初始无 ifindex，依赖桥接补全 | 触发 `td_switch_mac_snapshot` | 回调中 `ifindex` 从 0 更新到设备实际值并产生 `MOD` |

### E. 接口事件与 Netlink
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| E01 | VLANIF IP 新增 | VLANIF 无 IP | 配置 IPv4 地址 | `terminal_manager_on_address_update` 日志；终端绑定恢复并回到 ACTIVE |
| E02 | VLANIF 删除 | VLANIF 有终端绑定 | 删除 IPv4 地址 | 所有关联终端转入 `IFACE_INVALID`；日志记录 address update |
| E03 | Netlink 线程恢复 | 人为关闭 netlink socket（`kill -STOP` + `kill -CONT`） | 观察日志 | 线程恢复后继续接收事件；无崩溃 |

### F. 适配器收发链路
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| F01 | ARP RX VLAN 恢复 | 适配器使用 `PACKET_AUXDATA` | 发送带 VLAN tag 的 ARP | 抓包显示终端发现正确记录 VLAN；回调数据 `vlan_id` 匹配 |
| F02 | ARP TX 速率限制 | `tx_interval_ms=100` | 启动 10 个终端同时触发探测 | 发送间隔 ≥100ms；日志无速率告警；抓包对齐 |
| F03 | TX Fallback | 指定终端 `tx_iface` 可用 | 模拟物理口发送失败（禁用物理接口） | 第一次发送失败后回退到虚接口，日志包含 fallback 成功 |
| F04 | 适配器停止清理 | 调用 `terminal_discovery_cleanup` 或宿主退出 | 观察线程与文件描述符 | `rx_thread` 停止；`send_lock` 不再持有；`netstat` 无残留套接字 |

### G. MAC 表桥接
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| G01 | 容量缓存策略 | 桥接弱符号仍生效 | 调用 `td_switch_mac_get_capacity` | 返回默认容量 1024；日志含 `[switch-mac-stub]` |
| G02 | 快照刷新 | 设置 `TD_SWITCH_MAC_STUB_COUNT=5` | 调用 `td_switch_mac_snapshot` | 返回 5 条目；终端表 `ifindex` 更新 |
| G03 | 缓冲区不足告警 | 将缓存设为 2 条 | 桥接返回 5 条数据 | 返回错误码并记录告警；终端事件保持旧数据 |

### H. 统计与观测
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| H01 | SIGUSR1 即时统计 | 管理器正常运行 | 向宿主发送 `SIGUSR1` | 日志打印 `terminal_stats`，字段与 `terminal_manager_get_stats` 一致 |
| H02 | 定期统计输出 | `stats_log_interval_sec=30` | 长时间运行 | 每 30 秒输出统计日志，内容随终端变化更新 |
| H03 | 日志级别调整 | 调整 `runtime_cfg.log_level=TD_LOG_DEBUG` | 重新初始化 | DEBUG 日志包含配置合并、netlink 启动等细节 |

### I. 失败注入与恢复
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| I01 | netlink 启动失败回滚 | 临时改动权限阻止 `NETLINK_ROUTE` | 启动实例 | 日志出现 `failed to start netlink listener`；初始化返回 -1；无适配器线程残留 |
| I02 | register_packet_rx 失败 | 修改适配器使其返回错误码 | 启动实例 | 返回对应错误码；`terminal_discovery_cleanup` 释放 `manager` 与 `adapter` |
| I03 | send_arp 错误恢复 | 强制适配器返回 `TD_ADAPTER_ERR_IO` | 观察探测 | 日志 WARN，终端保持 PROBING 并按阈值删除；无崩溃 |

### J. 性能与压力
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| J01 | 300 终端性能基线 | 流量发生器模拟 300 终端 | 运行 30 分钟 | CPU 单核占用 <10%；无事件丢失；日志无容量丢弃 |
| J02 | 1000 终端压力 | 模拟 1000 终端，保持 1pps Keepalive | 运行 60 分钟 | CPU < 单核 50%；`capacity_drops=0`；回调与日志无 backlog |
| J03 | 冷启动耗时 | 从进程启动到 `adapter start` | 记录启动时间 | <2s 完成；日志顺序正确 |

### K. 边界配置
| 用例ID | 目标 | 前置条件 | 步骤 | 预期结果 |
| --- | --- | --- | --- | --- |
| K01 | 最小 keepalive 周期 | 将 `keepalive_interval_sec=10`，`keepalive_miss_threshold=1` | 运行终端 | 快速探测并删除无响应终端；无死锁 |
| K02 | 最大终端阈值 | 设置 `max_terminals=1000`，溢出 50 个终端 | 注入 1050 MAC | 超出部分被拒绝并记录 `capacity_drops`；回调不包含超限终端 |
| K03 | IFACE holdoff 短缩 | `iface_invalid_holdoff_sec=60` | 触发接口失效 | 60s 后终端删除；日志标记 holdoff 到期 |

## 记录与报告
- 对每个用例记录：
  - 执行日期、操作者、设备序列号、固件版本。
  - 关键日志片段、抓包文件、回调输出、性能指标（CPU、内存、线程数）。
  - 是否符合预期及偏差原因。
- 测试完成后输出总结，覆盖：
  - 成功率统计、失败用例整改计划。
  - 性能对比表（200/300/500/1000 档位 CPU/内存）。
  - 回滚方案演练记录（进程退出、配置恢复）。

## 附录
- 推荐命令：
  - `TD_LOG_LEVEL=debug ./host_daemon` 调整日志级别。
  - `TD_SWITCH_MAC_STUB_COUNT=5 ./host_daemon --dry-run` 验证桥接桩。
  - `kill -USR1 <pid>` 手工触发统计输出。
  - `tcpdump -i eth0 -vvv vlan and arp` 现场抓包。
- 产出模板：`doc/notes/test_report_template.md`（如需，后续可补充）。
