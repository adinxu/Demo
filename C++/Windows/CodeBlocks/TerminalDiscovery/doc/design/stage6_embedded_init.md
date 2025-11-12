# 阶段 6：守护进程嵌入式初始化入口

终端发现代理现已提供可嵌入外部守护进程的初始化接口，便于业务进程直接装载并托管终端发现生命周期。本节梳理 API 形态、运行流程、错误码约定以及嵌入侧的使用建议。

## API 概览

- 头文件：`src/include/terminal_discovery_embed.h`
- 初始化入口：`int terminal_discovery_initialize(const struct terminal_discovery_init_params *params);`
- 只读 accessor：
  - `struct terminal_manager *terminal_discovery_get_manager(void);`
  - `const struct app_context *terminal_discovery_get_app_context(void);`
- 入口位于：`src/main/terminal_main.c`

### 参数结构 `terminal_discovery_init_params`

| 字段 | 说明 |
| ---- | ---- |
| `runtime_config` | （可选）指向 `td_runtime_config` 的完整覆写。若为 `NULL`，函数自动加载默认值。调用方可先调用 `td_config_load_defaults`，再按需修改字段。结构体将按值拷贝，后续对原始内存的修改不会影响运行实例。 |

### 初始化流程摘要

1. 加载默认配置并合并 `runtime_config` 覆写，随后同步日志级别到 `runtime_cfg.log_level`。
2. 通过 `td_adapter_registry_find` 定位适配器描述符，构造 `td_adapter_config`/`td_adapter_env` 并调用 `ops->init`。
3. 将 `td_runtime_config` 转换为 `terminal_manager_config`，创建 `terminal_manager`；若适配器或管理器失败会立即回滚。
4. 启动 netlink 监听器，随后调用 `terminal_northbound_attach_default_sink` 绑定默认日志回调，确保外部暂未注册增量上报时也能观测事件。
5. 订阅报文回调 `register_packet_rx` 并启动适配器；成功后 `terminal_discovery_initialize` 返回 0，随后实例常驻运行。
6. 任意步骤失败都会触发 `terminal_discovery_cleanup`，按启动逆序停止适配器、netlink 与管理器，并复原内部上下文。

> 📌 初始化函数不会注册信号处理器、不会挂载 CLI，也不会开启周期性统计日志；宿主进程需自行实现这些特性。

## 错误码与回滚策略

| 返回值 | 场景 |
| ------ | ---- |
| `-EINVAL` | `params == NULL`。 |
| `-EALREADY` | 进程内已成功调用一次初始化，拒绝重复启动。 |
| `-EIO` | 默认配置加载失败（极少发生）。 |
| `-ENOENT` | 指定的适配器不存在（由 `td_adapter_registry_find` 返回）。 |
| `TD_ADAPTER_ERR_*` | 适配器生命周期（`init/register_packet_rx/start`）中的具体失败值会原样透出。 |
| 其他负值 | `terminal_netlink_start`、`terminal_northbound_attach_default_sink`（含 `terminal_manager_set_event_sink`）、`td_config_to_manager_config` 或内部清理路径返回的错误，均会回滚并传递该值。 |

清理过程保证：

- 若 netlink 已启动则调用 `terminal_netlink_stop`；
- 若事件回调已注册则在销毁前解绑；
- 释放 `terminal_manager`，最后执行 `adapter->shutdown`；
- 所有统计数据在销毁前通过 `terminal_manager_flush_events` 立即排空，避免残留未送出的批次。

## 使用建议

1. 宿主进程在 main 早期调用；若需修改默认配置，可按下列步骤：
   ```c
   struct td_runtime_config cfg;
   td_config_load_defaults(&cfg);
   cfg.stats_log_interval_sec = 30;
   cfg.max_terminals = 200;

   struct terminal_discovery_init_params params = {
       .runtime_config = &cfg,
   };
   int rc = terminal_discovery_initialize(&params);
   ```
  初始化默认挂接日志 sink；若宿主暂不消费增量通知，可保持默认配置，通过 INFO 级别日志观察事件。
2. 由于配置按值拷贝，初始化返回后无需保持 `cfg` 生命周期；仍可记录副本供调试。
3. 若需要手动触发统计输出，可调用 `terminal_discovery_get_manager()` 获取管理器句柄，再结合 `terminal_manager_get_stats` 输出统计；亦可缓存 `terminal_discovery_get_app_context()` 结果以备扩展使用（返回指针仅供只读查询）。
4. 当前版本默认与进程同生共死，未提供显式停止 API；宿主退出即可释放所有资源。
5. 若直接将 `terminal_main.c` 编译进宿主可执行文件，需在编译参数中定义 `TD_DISABLE_APP_MAIN`（例如 `-DTD_DISABLE_APP_MAIN`），以屏蔽 CLI 的 `main` 实现并避免链接阶段出现重复入口符号。
6. 套件仍依赖 `td_log_writef`，宿主可在初始化前调用 `td_log_set_level` 与自定义 `td_log_set_sink`（如需）来对接自身日志系统，确保输出格式一致。

### 宿主侧集成补充

- **编译期**：保证引用终端发现源码或静态库时传递 `TD_DISABLE_APP_MAIN`，典型示例如下：
  ```sh
  gcc -DTD_DISABLE_APP_MAIN -I./src/include -c src/main/terminal_main.c
  ```
  若使用现有 Makefile，可通过环境变量传入：`make CFLAGS+="-DTD_DISABLE_APP_MAIN"`。
- **链接期**：嵌入式宿主仅需链接 `terminal_main.o` 及相关依赖（适配器、管理器、netlink 等对象）；CLI 入口会因宏定义被剔除，不会与宿主 `main` 冲突。
- **运行期**：宿主按需负责信号处理、统计调度与退出流程，可在收到终止信号后直接结束进程，终端发现会在进程回收时自动清理。

示例：

```c
struct terminal_manager_stats stats;
memset(&stats, 0, sizeof(stats));

struct terminal_manager *mgr = terminal_discovery_get_manager();
if (mgr) {
  terminal_manager_get_stats(mgr, &stats);
  td_log_writef(TD_LOG_INFO, "embedded", "active=%" PRIu64, stats.current_terminals);
}
```

## 测试覆盖

- `tests/terminal_embedded_init_tests.c` 基于 stub 适配器与 netlink 打桩验证成功路径、重复调用保护，以及各类失败场景（适配器 init/netlink 启动/事件注册/报文订阅等）所触发的回滚流程。
- 新测试已集成在 `make test`，运行序列：
  ```sh
  cd src
  make test
  ```
  该命令会依次执行状态机单测、集成测试、MAC 桩测试与嵌入初始化测试。

## 集成清单

- `terminal_discovery_embed.h` 对外暴露 `terminal_discovery_init_params` 与初始化函数。
- `terminal_main.c` 共享 CLI 与嵌入式启动路径，通过内部 helper 确保两条入口的一致性。
- `src/Makefile` 增补 `terminal_embedded_init_tests` 目标，并在 `make test` 中默认执行。
- 文档《阶段 6 嵌入入口》记录 API 细节，便于宿主团队接入。
