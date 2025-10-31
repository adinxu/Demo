/*
需求描述:目前需要实现一个通用的，可跨平台移植(例如arm64,mips)的c程序，实现二层报文处理功能，其功能可描述为
1.在本机所有接口接收二层广播报文(目的地址为ff:ff:ff:ff:ff:ff)且二层报文类型为0x3689的报文
2.对接收到的报文进行处理，将报文的Data字段的值提取出来，保存备用
3.获取接收报文的接口的mac地址，保存备用
4.将处理后的报文重新封装成二层报文，目的地址为接收报文的源mac地址，源地址为步骤3获取的接收报文的接口的mac地址，二层报文类型为0x3689
5.将重新封装的报文发送出去，发送接口为报文接收所在接口
6.动态监控网络接口状态变化，当接口UP时自动开始监听，接口DOWN时自动停止监听
7.自动发现新增的网络接口并开始监听(排除虚拟接口如loopback、docker、veth等)

非功能需求:
1.程序需具备良好的可读性，易于理解
2.程序需具备良好的可维护性，易于扩展
3.程序需具备良好的健壮性，能处理异常情况，比如接口up时要新增监听，接口down要去除监听，采用netlink实时监控接口状态变化，避免轮询开销
4.程序需具备良好的性能，能高效处理报文，采用多线程架构：每个网络接口独立工作线程+netlink状态监控线程
5.程序需具备良好的跨平台移植性，支持不同CPU架构(arm64、mips等)

技术方案:
1.使用C语言编写程序，确保程序的可移植性和跨平台性
2.使用libpcap库进行报文的捕获和发送，确保程序的跨平台性，支持Linux/Unix系统
3.使用BPF过滤器优化报文捕获性能，仅捕获目标类型报文(ether dst ff:ff:ff:ff:ff:ff and ether proto 0x3689)
4.使用Linux netlink socket实时监控网络接口状态变化，避免轮询开销
5.使用标准库函数进行字符串处理和内存管理，确保程序的可读性和可维护性
6.使用结构体定义报文格式和上下文信息，确保程序的可读性和易于理解
7.使用函数进行模块化设计，确保程序的可维护性和易于扩展
8.使用完善的错误处理机制，确保程序的健壮性：参数验证、资源管理、异常恢复
9.使用多线程架构提升性能：
   - 主线程：程序初始化和清理
   - 每个网络接口一个独立的工作线程：负责报文捕获和处理
   - netlink监控线程：实时监控接口状态变化
   - 线程同步：使用pthread_mutex保护共享资源
10.使用高效的算法和数据结构，确保程序的性能：
    - 接口管理：数组+线性查找(适合小规模接口数量)
    - 内存管理：栈上分配为主，避免频繁动态分配
    - 报文处理：零拷贝策略，直接操作捕获缓冲区
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pcap.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <time.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

// 常量定义
#define ETH_TYPE_CUSTOM 0x3689
#define MAX_DATA_LEN 1500
#define MAX_INTERFACES 32
#define PACKET_TIMEOUT 1000
#define FILTER_EXPRESSION "ether dst ff:ff:ff:ff:ff:ff and ether proto 0x3689"
#define DEBUG 1 // 设置为1以启用调试模式
#define NETLINK_BUFFER_SIZE 4096

// 以太网帧结构
typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t eth_type;
    uint8_t data[MAX_DATA_LEN];
} __attribute__((packed)) custom_eth_frame_t;

// 报文信息结构
typedef struct {
    uint8_t data[MAX_DATA_LEN];
    size_t data_len;
    uint8_t iface_mac[6];
    uint8_t src_mac[6];
    char iface_name[IFNAMSIZ];
} packet_info_t;

// 接口处理上下文
typedef struct {
    pcap_t *pcap_handle;
    char iface_name[IFNAMSIZ];
    uint8_t iface_mac[6];
    pthread_t thread_id;
    int active;
    int interface_up;
    pthread_mutex_t ctx_mutex;
} interface_ctx_t;

// 全局变量
static volatile int g_running = 1;
static interface_ctx_t g_interfaces[MAX_INTERFACES];
static int g_interface_count = 0;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_interfaces_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_netlink_thread;
static int g_netlink_fd = -1;

// 函数声明
static void signal_handler(int signum);
static int get_interface_mac(const char *iface, uint8_t *mac);
static int is_interface_suitable(const char *name);
static int is_interface_up(const char *name);
static int validate_ethernet_frame(const u_char *packet, int len);
static int process_custom_packet(const custom_eth_frame_t *frame, int len, packet_info_t *info);
static int send_response_packet(pcap_t *handle, const packet_info_t *info);
static void* interface_worker(void *arg);
static void* netlink_monitor(void *arg);
static void packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet);
static int setup_packet_filter(pcap_t *handle, const char *interface_name);
static int initialize_interface(const char *name, interface_ctx_t *ctx);
static int start_interface_monitoring(interface_ctx_t *ctx);
static int stop_interface_monitoring(interface_ctx_t *ctx);
static void cleanup_interface(interface_ctx_t *ctx);
static int setup_netlink_socket(void);
static void handle_netlink_message(struct nlmsghdr *nlh);
static void handle_interface_event(const char *name, int is_up);
static interface_ctx_t* find_interface_by_name(const char *name);
static int add_new_interface(const char *name);
static void log_message(const char *level, const char *format, ...);
static void print_mac_address(const uint8_t *mac);

// 信号处理函数
static void signal_handler(int signum) {
    log_message("INFO", "接收到信号 %d，准备退出程序", signum);
    g_running = 0;
}

// 获取接口MAC地址
static int get_interface_mac(const char *iface, uint8_t *mac) {
    if (!iface || !mac) {
        log_message("ERROR", "get_interface_mac: 参数为空");
        return -1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_message("ERROR", "创建socket失败: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
        log_message("ERROR", "获取接口 %s MAC地址失败: %s", iface, strerror(errno));
        close(fd);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(fd);
    return 0;
}

// 检查接口是否适合使用
static int is_interface_suitable(const char *name) {
    if (!name) {
        return 0;
    }

    // 排除已知的虚拟接口和特殊接口
    const char *excluded_interfaces[] = {
        "any",          // pcap的特殊接口
        "lo",           // 回环接口
        "dummy",        // dummy接口
        "nflog",        // netfilter日志接口
        "nfqueue",      // netfilter队列接口
        "bluetooth",    // 蓝牙接口
        "usb",          // USB接口
        "bond",         // 绑定接口
        "br-",          // 桥接接口
        "docker",       // Docker接口
        "veth",         // 虚拟以太网接口
        "tun",          // TUN接口
        "tap",          // TAP接口
        NULL
    };

    // 检查接口名称是否在排除列表中
    for (int i = 0; excluded_interfaces[i] != NULL; i++) {
        if (strncmp(name, excluded_interfaces[i], strlen(excluded_interfaces[i])) == 0) {
            log_message("DEBUG", "跳过接口 %s (类型: %s)", name, excluded_interfaces[i]);
            return 0;
        }
    }

    return 1;
}

// 检查接口是否处于UP状态
static int is_interface_up(const char *name) {
    if (!name) {
        return 0;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        log_message("DEBUG", "创建socket失败: %s", strerror(errno));
        return 0;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    int result = 0;
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        if (ifr.ifr_flags & IFF_UP) {
            result = 1;
        } else {
            log_message("DEBUG", "接口 %s 未启用", name);
        }
    } else {
        log_message("DEBUG", "获取接口 %s 状态失败: %s", name, strerror(errno));
    }

    close(fd);
    return result;
}

// 验证以太网帧
static int validate_ethernet_frame(const u_char *packet, int len) {
    if (!packet || len < sizeof(struct ether_header)) {
        return -1;
    }

#ifdef DEBUG
    // 调试模式下进行完整校验，用于验证BPF过滤器的正确性
    const struct ether_header *eth_header = (const struct ether_header *)packet;
    
    const uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (memcmp(eth_header->ether_dhost, broadcast_mac, 6) != 0) {
        log_message("WARNING", "BPF过滤器异常：非广播报文通过了过滤");
        return -1;
    }

    if (ntohs(eth_header->ether_type) != ETH_TYPE_CUSTOM) {
        log_message("WARNING", "BPF过滤器异常：错误的以太网类型 0x%04x", ntohs(eth_header->ether_type));
        return -1;
    }
#endif

    return 0;
}

// 处理自定义报文
static int process_custom_packet(const custom_eth_frame_t *frame, int len, packet_info_t *info) {
    if (!frame || !info || len < sizeof(struct ether_header)) {
        return -1;
    }

    // 计算数据长度
    size_t data_len = len - sizeof(struct ether_header);
    if (data_len > MAX_DATA_LEN) {
        data_len = MAX_DATA_LEN;
    }

    // 保存数据
    memcpy(info->data, frame->data, data_len);
    info->data_len = data_len;
    memcpy(info->src_mac, frame->src_mac, 6);

    return 0;
}

// 发送响应报文
static int send_response_packet(pcap_t *handle, const packet_info_t *info) {
    if (!handle || !info) {
        log_message("ERROR", "send_response_packet: 参数为空");
        return -1;
    }

    custom_eth_frame_t response_frame;
    memset(&response_frame, 0, sizeof(response_frame));

    // 设置目的MAC为原报文的源MAC
    memcpy(response_frame.dst_mac, info->src_mac, 6);
    // 设置源MAC为接口MAC
    memcpy(response_frame.src_mac, info->iface_mac, 6);
    // 设置以太网类型
    response_frame.eth_type = htons(ETH_TYPE_CUSTOM);
    // 复制数据
    memcpy(response_frame.data, info->data, info->data_len);

    size_t packet_size = sizeof(struct ether_header) + info->data_len;
    
    if (pcap_inject(handle, &response_frame, packet_size) == -1) {
        log_message("ERROR", "发送报文失败: %s", pcap_geterr(handle));
        return -1;
    }

    log_message("INFO", "成功发送响应报文，长度: %zu", packet_size);
    return 0;
}

// 报文处理回调函数
static void packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet) {
    interface_ctx_t *ctx = (interface_ctx_t *)user;
    
    if (!ctx || !header || !packet || !g_running) {
        return;
    }

    // 验证以太网帧
    if (validate_ethernet_frame(packet, header->caplen) != 0) {
        return;
    }

    const custom_eth_frame_t *frame = (const custom_eth_frame_t *)packet;
    packet_info_t pkt_info;
    memset(&pkt_info, 0, sizeof(pkt_info));

    // 处理报文
    if (process_custom_packet(frame, header->caplen, &pkt_info) != 0) {
        log_message("ERROR", "处理报文失败");
        return;
    }

    // 设置接口信息
    memcpy(pkt_info.iface_mac, ctx->iface_mac, 6);
    strncpy(pkt_info.iface_name, ctx->iface_name, IFNAMSIZ - 1);
    pkt_info.iface_name[IFNAMSIZ - 1] = '\0';

    log_message("INFO", "接收到报文，接口: %s，数据长度: %zu", 
                ctx->iface_name, pkt_info.data_len);

    // 发送响应
    if (send_response_packet(ctx->pcap_handle, &pkt_info) != 0) {
        log_message("ERROR", "发送响应报文失败");
    }
}

// 设置报文过滤器
static int setup_packet_filter(pcap_t *handle, const char *interface_name) {
    struct bpf_program filter_program;
    
    if (pcap_compile(handle, &filter_program, FILTER_EXPRESSION, 1, PCAP_NETMASK_UNKNOWN) == -1) {
        log_message("ERROR", "编译过滤器失败，接口 %s: %s", interface_name, pcap_geterr(handle));
        return -1;
    }

    if (pcap_setfilter(handle, &filter_program) == -1) {
        log_message("ERROR", "设置过滤器失败，接口 %s: %s", interface_name, pcap_geterr(handle));
        pcap_freecode(&filter_program);
        return -1;
    }

    pcap_freecode(&filter_program);
    return 0;
}

// 设置netlink socket
static int setup_netlink_socket(void) {
    struct sockaddr_nl addr;
    
    g_netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (g_netlink_fd < 0) {
        log_message("ERROR", "创建netlink socket失败: %s", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;
    
    if (bind(g_netlink_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_message("ERROR", "绑定netlink socket失败: %s", strerror(errno));
        close(g_netlink_fd);
        g_netlink_fd = -1;
        return -1;
    }
    
    log_message("INFO", "Netlink socket设置成功");
    return 0;
}

// 处理netlink消息
static void handle_netlink_message(struct nlmsghdr *nlh) {
    struct ifinfomsg *ifi;
    struct rtattr *rta;
    int len;
    char ifname[IFNAMSIZ] = {0};
    int is_up = 0;
    
    if (nlh->nlmsg_type != RTM_NEWLINK && nlh->nlmsg_type != RTM_DELLINK) {
        return;
    }
    
    ifi = NLMSG_DATA(nlh);
    len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
    
    // 解析属性
    for (rta = IFLA_RTA(ifi); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        if (rta->rta_type == IFLA_IFNAME) {
            strncpy(ifname, RTA_DATA(rta), IFNAMSIZ - 1);
            ifname[IFNAMSIZ - 1] = '\0';
            break;
        }
    }
    
    if (strlen(ifname) == 0) {
        return;
    }
    
    // 检查接口状态
    is_up = (ifi->ifi_flags & IFF_UP) ? 1 : 0;
    
    log_message("DEBUG", "Netlink事件: 接口 %s %s", ifname, is_up ? "UP" : "DOWN");
    
    handle_interface_event(ifname, is_up);
}

// 查找接口
static interface_ctx_t* find_interface_by_name(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < g_interface_count; i++) {
        if (strcmp(g_interfaces[i].iface_name, name) == 0) {
            return &g_interfaces[i];
        }
    }
    return NULL;
}

// 添加新接口
static int add_new_interface(const char *name) {
    if (!name || g_interface_count >= MAX_INTERFACES) {
        return -1;
    }
    
    if (!is_interface_suitable(name)) {
        return -1;
    }
    
    interface_ctx_t *ctx = &g_interfaces[g_interface_count];
    
    // 初始化上下文
    memset(ctx, 0, sizeof(interface_ctx_t));
    pthread_mutex_init(&ctx->ctx_mutex, NULL);
    strncpy(ctx->iface_name, name, IFNAMSIZ - 1);
    ctx->iface_name[IFNAMSIZ - 1] = '\0';
    
    // 获取MAC地址
    if (get_interface_mac(name, ctx->iface_mac) != 0) {
        log_message("DEBUG", "获取新接口MAC地址失败: %s", name);
        return -1;
    }
    
    ctx->interface_up = is_interface_up(name);
    g_interface_count++;
    
    log_message("INFO", "添加新接口: %s，MAC: ", name);
    print_mac_address(ctx->iface_mac);
    
    return 0;
}

// 处理接口事件
static void handle_interface_event(const char *name, int is_up) {
    if (!name) return;
    
    pthread_mutex_lock(&g_interfaces_mutex);
    
    interface_ctx_t *ctx = find_interface_by_name(name);
    
    if (!ctx) {
        // 新接口出现且状态为UP
        if (is_up) {
            if (add_new_interface(name) == 0) {
                ctx = find_interface_by_name(name);
                if (ctx && start_interface_monitoring(ctx) == 0) {
                    log_message("INFO", "新接口 %s 已开始监听", name);
                }
            }
        }
        pthread_mutex_unlock(&g_interfaces_mutex);
        return;
    }
    
    // 接口状态变化
    pthread_mutex_lock(&ctx->ctx_mutex);
    int was_up = ctx->interface_up;
    ctx->interface_up = is_up;
    pthread_mutex_unlock(&ctx->ctx_mutex);
    
    if (is_up && !was_up) {
        // 接口从DOWN变为UP
        log_message("INFO", "接口 %s UP", name);
        if (start_interface_monitoring(ctx) == 0) {
            log_message("INFO", "接口 %s 监听已启动", name);
        }
    } else if (!is_up && was_up) {
        // 接口从UP变为DOWN
        log_message("INFO", "接口 %s DOWN", name);
        if (stop_interface_monitoring(ctx) == 0) {
            log_message("INFO", "接口 %s 监听已停止", name);
        }
    }
    
    pthread_mutex_unlock(&g_interfaces_mutex);
}

// 启动接口监听
static int start_interface_monitoring(interface_ctx_t *ctx) {
    if (!ctx) return -1;

    pthread_mutex_lock(&ctx->ctx_mutex);
    
    if (ctx->active || ctx->pcap_handle) {
        pthread_mutex_unlock(&ctx->ctx_mutex);
        return 0; // 已经在运行
    }

    char errbuf[PCAP_ERRBUF_SIZE];
    
    // 打开接口
    ctx->pcap_handle = pcap_open_live(ctx->iface_name, 65536, 1, PACKET_TIMEOUT, errbuf);
    if (!ctx->pcap_handle) {
        log_message("ERROR", "打开接口失败 %s: %s", ctx->iface_name, errbuf);
        pthread_mutex_unlock(&ctx->ctx_mutex);
        return -1;
    }

    // 设置过滤器
    if (setup_packet_filter(ctx->pcap_handle, ctx->iface_name) != 0) {
        pcap_close(ctx->pcap_handle);
        ctx->pcap_handle = NULL;
        pthread_mutex_unlock(&ctx->ctx_mutex);
        return -1;
    }

    ctx->active = 1;
    
    // 创建监听线程
    if (pthread_create(&ctx->thread_id, NULL, interface_worker, ctx) != 0) {
        log_message("ERROR", "创建监听线程失败，接口: %s", ctx->iface_name);
        pcap_close(ctx->pcap_handle);
        ctx->pcap_handle = NULL;
        ctx->active = 0;
        pthread_mutex_unlock(&ctx->ctx_mutex);
        return -1;
    }

    pthread_mutex_unlock(&ctx->ctx_mutex);
    return 0;
}

// 停止接口监听
static int stop_interface_monitoring(interface_ctx_t *ctx) {
    if (!ctx) return -1;

    pthread_mutex_lock(&ctx->ctx_mutex);
    
    if (!ctx->active) {
        pthread_mutex_unlock(&ctx->ctx_mutex);
        return 0; // 已经停止
    }

    ctx->active = 0;
    
    if (ctx->pcap_handle) {
        pcap_breakloop(ctx->pcap_handle);
    }

    pthread_mutex_unlock(&ctx->ctx_mutex);

    // 等待线程结束
    if (ctx->thread_id) {
        pthread_join(ctx->thread_id, NULL);
        ctx->thread_id = 0;
    }

    pthread_mutex_lock(&ctx->ctx_mutex);
    if (ctx->pcap_handle) {
        pcap_close(ctx->pcap_handle);
        ctx->pcap_handle = NULL;
    }
    pthread_mutex_unlock(&ctx->ctx_mutex);

    return 0;
}

// netlink监控线程
static void* netlink_monitor(void *arg) {
    (void)arg; // 未使用的参数
    
    char buffer[NETLINK_BUFFER_SIZE];
    struct nlmsghdr *nlh;
    int len;
    
    log_message("INFO", "启动Netlink监控线程");
    
    while (g_running) {
        len = recv(g_netlink_fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            log_message("ERROR", "接收netlink消息失败: %s", strerror(errno));
            break;
        }
        
        for (nlh = (struct nlmsghdr *)buffer; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                break;
            }
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                log_message("ERROR", "Netlink错误消息");
                continue;
            }
            
            handle_netlink_message(nlh);
        }
    }
    
    log_message("INFO", "Netlink监控线程退出");
    return NULL;
}

// 初始化接口
static int initialize_interface(const char *name, interface_ctx_t *ctx) {
    if (!name || !ctx) {
        return -1;
    }

    // 检查接口是否适合使用
    if (!is_interface_suitable(name)) {
        log_message("DEBUG", "接口 %s 不适合使用，跳过", name);
        return -1;
    }

    // 初始化上下文
    memset(ctx, 0, sizeof(interface_ctx_t));
    pthread_mutex_init(&ctx->ctx_mutex, NULL);
    strncpy(ctx->iface_name, name, IFNAMSIZ - 1);
    ctx->iface_name[IFNAMSIZ - 1] = '\0';

    // 获取接口MAC地址
    if (get_interface_mac(name, ctx->iface_mac) != 0) {
        log_message("DEBUG", "获取接口MAC地址失败: %s", name);
        return -1;
    }

    // 设置初始状态
    ctx->interface_up = is_interface_up(name);

    log_message("INFO", "成功初始化接口: %s，MAC: ", name);
    print_mac_address(ctx->iface_mac);

    // 如果接口是UP状态，开始监听
    if (ctx->interface_up) {
        if (start_interface_monitoring(ctx) != 0) {
            log_message("ERROR", "启动接口监听失败: %s", name);
            return -1;
        }
    } else {
        log_message("INFO", "接口 %s 当前DOWN状态，等待UP后开始监听", name);
    }

    return 0;
}

// 清理接口资源
static void cleanup_interface(interface_ctx_t *ctx) {
    if (!ctx) return;

    stop_interface_monitoring(ctx);
    pthread_mutex_destroy(&ctx->ctx_mutex);
}

// 接口工作线程
static void* interface_worker(void *arg) {
    interface_ctx_t *ctx = (interface_ctx_t *)arg;
    
    if (!ctx) {
        log_message("ERROR", "接口工作线程参数错误");
        return NULL;
    }

    log_message("INFO", "启动接口监听线程: %s", ctx->iface_name);

    // 开始捕获报文
    while (g_running) {
        pthread_mutex_lock(&ctx->ctx_mutex);
        if (!ctx->active || !ctx->pcap_handle) {
            pthread_mutex_unlock(&ctx->ctx_mutex);
            break;
        }
        pthread_mutex_unlock(&ctx->ctx_mutex);

        int result = pcap_dispatch(ctx->pcap_handle, 10, packet_handler, (u_char *)ctx);
        if (result == -1) {
            log_message("ERROR", "pcap_dispatch失败，接口 %s: %s", 
                       ctx->iface_name, pcap_geterr(ctx->pcap_handle));
            break;
        } else if (result == 0) {
            // 超时，继续循环
            usleep(10000); // 10ms
        }
    }

    log_message("INFO", "接口监听线程退出: %s", ctx->iface_name);
    return NULL;
}

// 日志记录函数
static void log_message(const char *level, const char *format, ...) {
    pthread_mutex_lock(&g_log_mutex);
    
    time_t now;
    struct tm *tm_info;
    char time_str[64];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] [%s] ", time_str, level);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
    
    pthread_mutex_unlock(&g_log_mutex);
}

// 打印MAC地址
static void print_mac_address(const uint8_t *mac) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// 主函数
int main() {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *all_devices = NULL;
    pcap_if_t *device = NULL;
    int result = 0;
    int attempted_interfaces = 0;

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    log_message("INFO", "启动二层报文处理程序");

    // 设置netlink监控
    if (setup_netlink_socket() != 0) {
        log_message("ERROR", "设置netlink监控失败");
        return 1;
    }

    // 查找所有网络接口
    if (pcap_findalldevs(&all_devices, errbuf) == -1) {
        log_message("ERROR", "查找网络接口失败: %s", errbuf);
        close(g_netlink_fd);
        return 1;
    }

    // 初始化接口
    for (device = all_devices; device && g_interface_count < MAX_INTERFACES; device = device->next) {
        // 跳过回环接口
        if (device->flags & PCAP_IF_LOOPBACK) {
            log_message("DEBUG", "跳过回环接口: %s", device->name);
            continue;
        }

        attempted_interfaces++;
        
        if (initialize_interface(device->name, &g_interfaces[g_interface_count]) == 0) {
            g_interface_count++;
        }
    }

    pcap_freealldevs(all_devices);

    log_message("INFO", "尝试初始化 %d 个接口，成功 %d 个", attempted_interfaces, g_interface_count);

    if (g_interface_count == 0) {
        log_message("INFO", "当前没有可用的网络接口，等待接口UP事件");
    } else {
        log_message("INFO", "成功初始化 %d 个网络接口", g_interface_count);
    }

    // 启动netlink监控线程
    if (pthread_create(&g_netlink_thread, NULL, netlink_monitor, NULL) != 0) {
        log_message("ERROR", "创建netlink监控线程失败");
        result = 1;
        goto cleanup;
    }

    // 等待程序退出
    while (g_running) {
        sleep(1);
    }

cleanup:
    log_message("INFO", "正在关闭程序...");

    // 停止netlink监控线程
    if (g_netlink_thread) {
        pthread_cancel(g_netlink_thread);
        pthread_join(g_netlink_thread, NULL);
    }

    // 关闭netlink socket
    if (g_netlink_fd >= 0) {
        close(g_netlink_fd);
    }

    // 清理所有接口
    for (int i = 0; i < g_interface_count; i++) {
        cleanup_interface(&g_interfaces[i]);
    }

    log_message("INFO", "程序已退出");
    return result;
}