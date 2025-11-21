#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/neighbour.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_addr.h>
#include <net/if.h> 
#include <fcntl.h>
//==================================================================================功能定制区 START==================================================================================//
// 通过命令行参数控制: --record <文件路径>  --filter-intf <接口名>
//==================================================================================功能定制区 END==================================================================================//


//RTMGRP_NEIGH
#define NETLINK_GROUP_LISTEN  (RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | \
                              RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE | RTMGRP_NEIGH)


// Netlink消息类型转文字描述
const char* nlmsg_type_to_str(__u16 nlmsg_type) {
    switch (nlmsg_type) {
        case RTM_NEWROUTE:  return "RTM_NEWROUTE";
        case RTM_DELROUTE:  return "RTM_DELROUTE";
        case RTM_GETROUTE:  return "RTM_GETROUTE";
        case RTM_NEWLINK:   return "RTM_NEWLINK";
        case RTM_DELLINK:   return "RTM_DELLINK";
        case RTM_NEWADDR:   return "RTM_NEWADDR";
        case RTM_DELADDR:   return "RTM_DELADDR";
        case RTM_NEWNEIGH:  return "RTM_NEWNEIGH";
        case RTM_DELNEIGH:  return "RTM_DELNEIGH";
        default:            return "UNKNOWN";
    }
}

// 路由协议类型转文字描述
const char* route_proto_to_str(__u8 proto) {
    switch (proto) {
        case RTPROT_STATIC:     return "STATIC";
        case RTPROT_KERNEL:     return "KERNEL";
        case RTPROT_BOOT:       return "BOOT";
        case RTPROT_UNSPEC:     return "UNSPEC";
        default:                return "OTHER";
    }
}
static const char *flags_to_str(unsigned short flags) {
    static char buf[128];
    char *p = buf;
    *p = '\0';

    if (flags & IFF_UP)          p += sprintf(p, "UP|");
    if (flags & IFF_BROADCAST)    p += sprintf(p, "BROADCAST|");
    if (flags & IFF_DEBUG)        p += sprintf(p, "DEBUG|");
    if (flags & IFF_LOOPBACK)     p += sprintf(p, "LOOPBACK|");
    if (flags & IFF_POINTOPOINT)  p += sprintf(p, "P2P|");
    if (flags & IFF_RUNNING)      p += sprintf(p, "RUNNING|");
    if (flags & IFF_NOARP)        p += sprintf(p, "NOARP|");
    if (flags & IFF_PROMISC)      p += sprintf(p, "PROMISC|");
    if (flags & IFF_MULTICAST)    p += sprintf(p, "MULTICAST|");
    if (flags & IFF_LOWER_UP)     p += sprintf(p, "LOWER_UP|");

    // 移除末尾多余的竖线
    if (p > buf) *(p - 1) = '\0';
    else strcpy(buf, "NONE");
    
    return buf;
}

void print_nlhead(struct nlmsghdr *nlh)
{
        // 打印消息头信息
        printf("\n==== Netlink Event ====\n");
        printf("Type: %s (%d)\n", 
                   nlmsg_type_to_str(nlh->nlmsg_type),  
                   nlh->nlmsg_type);
        printf("Seq: %u, PID: %u\n", nlh->nlmsg_seq, nlh->nlmsg_pid);
}

void parse_rtmsg(struct nlmsghdr *nlh) {
    struct rtmsg *rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    char dst_buf[INET6_ADDRSTRLEN] = "default";
    char gw_buf[INET6_ADDRSTRLEN] = "none";
    char ifname[IFNAMSIZ] = "unknown";
    int ifindex = 0;
    void *attr_value;

    int attr_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*rtm));
    struct rtattr *rta = RTM_RTA(rtm);

    // 判断地址族（IPv4 或 IPv6）
    int family = rtm->rtm_family;
    int addr_len = (family == AF_INET6) ? sizeof(struct in6_addr) : sizeof(struct in_addr);

    // 遍历路由属性
    for (; RTA_OK(rta, attr_len); rta = RTA_NEXT(rta, attr_len)) {
        switch (rta->rta_type) {
            case RTA_DST:  // 目的地址
                if (rta->rta_len >= RTA_LENGTH(addr_len)) {
                    attr_value = RTA_DATA(rta);
                    inet_ntop(family, attr_value, dst_buf, sizeof(dst_buf));
                }
                break;

            case RTA_GATEWAY:  // 网关地址
                if (rta->rta_len >= RTA_LENGTH(addr_len)) {
                    attr_value = RTA_DATA(rta);
                    inet_ntop(family, attr_value, gw_buf, sizeof(gw_buf));
                }
                break;

            case RTA_OIF:  // 出接口索引
                if (rta->rta_len >= RTA_LENGTH(sizeof(int))) {
                    ifindex = *(int *)RTA_DATA(rta);
                    // 尝试获取接口名（可能失败）
                    if_indextoname(ifindex, ifname);
                }
                break;
        }
    }

    // 打印解析结果
    printf("    Address Family: %s\n", (family == AF_INET6) ? "IPv6" : "IPv4");
    printf("    Type: %s\n", route_proto_to_str(rtm->rtm_protocol));
    printf("    Dest: %s/%d\n", dst_buf, rtm->rtm_dst_len);
    printf("    Gateway: %s\n", gw_buf);
    printf("    Interface: %s (%d)\n", ifname, ifindex);
    printf("    Table: %d\n", rtm->rtm_table);
}

void parse_addrmsg(struct nlmsghdr *nlh) {
        struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
        // 计算属性负载的长度 = 消息总长 - 头部 - ifaddrmsg大小
        int payload_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    static char buf[128];  // 静态缓冲区存储结果
    char ip_str[INET6_ADDRSTRLEN] = "unknown";
    char ifname[IFNAMSIZ] = "unknown";

    // 遍历属性获取IP地址
    struct rtattr *rta = IFA_RTA(ifa);
    for (; RTA_OK(rta, payload_len); rta = RTA_NEXT(rta, payload_len)) {
        if (rta->rta_type == IFA_ADDRESS) {
            void *addr_data = RTA_DATA(rta);
            
            // 根据地址族转换IP地址
            if (ifa->ifa_family == AF_INET) {
                struct in_addr *addr = addr_data;
                inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
                
                // IPv4 - 同时显示点分十进制掩码
                uint32_t mask = (0xffffffff << (32 - ifa->ifa_prefixlen)) & 0xffffffff;
                struct in_addr mask_addr = { .s_addr = htonl(mask) };
                char mask_str[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET, &mask_addr, mask_str, sizeof(mask_str));
                
                snprintf(buf, sizeof(buf), "IP: %s/%d (Mask: %s)", 
                         ip_str, ifa->ifa_prefixlen, mask_str);
                goto out;
            } 
            else if (ifa->ifa_family == AF_INET6) {
                struct in6_addr *addr6 = addr_data;
                inet_ntop(AF_INET6, addr6, ip_str, sizeof(ip_str));
                
                snprintf(buf, sizeof(buf), "IP: %s/%d", 
                         ip_str, ifa->ifa_prefixlen);
                 goto out;
            }
        }
    }

not_found:
    // 如果找不到地址属性，显示基本信息
    snprintf(buf, sizeof(buf), "Family: %d, Prefixlen: %d", 
             ifa->ifa_family, ifa->ifa_prefixlen);
        return;
                         
out:
        if_indextoname(ifa->ifa_index, ifname);
        printf("  Address event: %s Intf %s (%d) %s\n", 
                (nlh->nlmsg_type == RTM_NEWADDR) ? "NEW" : "DEL",
                ifname, ifa->ifa_index,
                buf);

    return;
}

// 添加缺少的NUD状态宏定义
#ifndef NUD_VALID
#define NUD_VALID (NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE|NUD_PROBE|NUD_STALE|NUD_DELAY)
#endif


static void parse_neighmsg(struct nlmsghdr *nlh) {
    struct ndmsg *ndm = NLMSG_DATA(nlh);
    struct rtattr *attr;
    int attr_len = RTM_PAYLOAD(nlh);
    char ip_str[INET6_ADDRSTRLEN] = {0};
    char mac_str[18] = {0}; // FF:FF:FF:FF:FF:FF + null
    char ifname[IFNAMSIZ] = "unknown";

        if_indextoname(ndm->ndm_ifindex, ifname);

    // 遍历消息属性
    for (attr = RTM_RTA(ndm); RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
        if (attr->rta_type == NDA_DST) { // IP地址
            if (ndm->ndm_family == AF_INET) {
                inet_ntop(AF_INET, RTA_DATA(attr), ip_str, sizeof(ip_str));
            } else if (ndm->ndm_family == AF_INET6) {
                inet_ntop(AF_INET6, RTA_DATA(attr), ip_str, sizeof(ip_str));
            }
        } else if (attr->rta_type == NDA_LLADDR) { // MAC地址
            unsigned char *mac = RTA_DATA(attr);
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }

    // 解析ARP状态(简化关键状态)
    const char *state_str;
    switch(ndm->ndm_state & NUD_VALID) {
        case NUD_REACHABLE: state_str = "REACHABLE"; break;
        case NUD_STALE:     state_str = "STALE";     break;
        case NUD_DELAY:     state_str = "DELAY";     break;
        case NUD_PROBE:     state_str = "PROBE";     break;
        case NUD_FAILED:    state_str = "FAILED";    break;
        default:            state_str = "UNKNOWN";   break;
    }

    printf("[%s] %s intf: %s (%d) mac:%s state:%s(%X)\n", 
           (nlh->nlmsg_type == RTM_NEWNEIGH) ? "ADD" : "DEL",
           ip_str, ifname, ndm->ndm_ifindex, mac_str, state_str, ndm->ndm_state);
out:
        return;
}

/* 重定向printf到文件 */
int redirect_stdout_to_file(const char *filename) {
    // 保存原始标准输出（用于恢复）
    int saved_stdout = dup(STDOUT_FILENO);
    
    // 尝试打开文件
    FILE *file = fopen(filename, "w");  // "a" = 追加模式,"w" = 覆盖模式
    if (!file) {
        perror("fopen failed");
        return -1;
    }
    
    // 将文件流设置为标准输出
    int fd = fileno(file);
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2 failed");
        fclose(file);
        return -1;
    }
    
    // 设置缓冲区模式（可选）
    //setvbuf(stdout, NULL, _IONBF, 0);  // 无缓冲模式
    
    // 关闭原始文件描述符（保留文件流打开）
    close(fd);
    
    return saved_stdout;  // 返回原始stdout用于恢复
}

/* 恢复原始标准输出 */
void restore_stdout(int saved_fd) {
    fflush(stdout);  // 确保输出被刷新
    
    // 恢复原始stdout
    if (dup2(saved_fd, STDOUT_FILENO) == -1) {
        perror("dup2 restore failed");
    }
    
    // 关闭保存的文件描述符
    close(saved_fd);
}


#define MAX_FILTER_INTF 8

int main(int argc, char *argv[]) {
    struct sockaddr_nl sa;
    int nl_sock;
    int record_to_file = 0;
    char file_to_write[256] = "";
    int filter_neigh_intf = 0;
    char neigh_filter_names[MAX_FILTER_INTF][IFNAMSIZ] = {0};
    int filter_intf_count = 0;
    int saved = -1;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--record") && i + 1 < argc) {
            record_to_file = 1;
            strncpy(file_to_write, argv[i + 1], sizeof(file_to_write) - 1);
            ++i;
        } else if (!strcmp(argv[i], "--filter-intf") && i + 1 < argc) {
            filter_neigh_intf = 1;
            if (filter_intf_count < MAX_FILTER_INTF) {
                strncpy(neigh_filter_names[filter_intf_count], argv[i + 1], IFNAMSIZ - 1);
                neigh_filter_names[filter_intf_count][IFNAMSIZ-1] = '\0';
                filter_intf_count++;
            }
            ++i;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s [--record <file>] [--filter-intf <name>]...\n", argv[0]);
            return 0;
        }
    }

    printf("Starting Netlink Monitor...\n");
    printf("Listening for netlink events (group: 0x%x)\n", NETLINK_GROUP_LISTEN);

    // 创建netlink socket
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = NETLINK_GROUP_LISTEN;

    if ((nl_sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 增大接收缓冲区减少溢出风险
    int rcvbuf_size = 1024 * 1024;  // 1MB
    if (setsockopt(nl_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        perror("warning: setsockopt SO_RCVBUF failed");
    }

    // 查询实际设置值
    int actual_size;
    socklen_t optlen = sizeof(actual_size);
    if (getsockopt(nl_sock, SOL_SOCKET, SO_RCVBUF, &actual_size, &optlen) < 0) {
        perror("getsockopt failed");
    } else {
        printf("Queried SO_RCVBUF value: %d bytes\n", actual_size);
    }

    char *buffer = malloc(rcvbuf_size);
    // 绑定socket
    if (bind(nl_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind failed");
        close(nl_sock);
        exit(EXIT_FAILURE);
    }

    printf("Monitoring started. Waiting for events...\n");

    if (record_to_file && file_to_write[0]) {
        printf("Write netlink event to file %s...\n", file_to_write);
        saved = redirect_stdout_to_file(file_to_write);
        if (saved < 0) exit(1);
    }

    while (1) {
        struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;
        ssize_t len;

        // 接收netlink消息
        if ((len = recv(nl_sock, buffer, rcvbuf_size, 0)) < 0) {
            perror("recv failed");
            continue;
        }

        // 处理多部分消息
        for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            int print_head = 1;
            if (filter_neigh_intf && (nlh->nlmsg_type == RTM_NEWNEIGH || nlh->nlmsg_type == RTM_DELNEIGH)) {
                struct ndmsg *ndm = NLMSG_DATA(nlh);
                char ifname[IFNAMSIZ] = "unknown";
                if_indextoname(ndm->ndm_ifindex, ifname);
                int matched = 0;
                for (int k = 0; k < filter_intf_count; ++k) {
                    if (strcmp(ifname, neigh_filter_names[k]) == 0) {
                        matched = 1;
                        break;
                    }
                }
                if (!matched) print_head = 0;
            }
            if (print_head) {
                print_nlhead(nlh);
            }

            // 解析特定消息类型
            switch (nlh->nlmsg_type) {
                case RTM_NEWROUTE:
                case RTM_DELROUTE:
                    if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(struct rtmsg))) {
                        parse_rtmsg(nlh);
                    }
                    break;

                // 其他事件类型
                case RTM_NEWLINK:
                case RTM_DELLINK: {
                    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
                    const char *event_str = (nlh->nlmsg_type == RTM_NEWLINK) ? "NEW" : "DEL";

                    printf("  Link event: %s (index %d, flags: 0x%04X -> %s)\n",
                           event_str,
                           ifi->ifi_index,
                           ifi->ifi_flags,
                           flags_to_str(ifi->ifi_flags));
                    break;
                }
                case RTM_NEWADDR:
                case RTM_DELADDR: {
                    parse_addrmsg(nlh);
                    break;
                }
                case RTM_NEWNEIGH:
                case RTM_DELNEIGH: {
                    // 只在指定接口时处理
                    if (filter_neigh_intf) {
                        struct ndmsg *ndm = NLMSG_DATA(nlh);
                        char ifname[IFNAMSIZ] = "unknown";
                        if_indextoname(ndm->ndm_ifindex, ifname);
                        int matched = 0;
                        for (int k = 0; k < filter_intf_count; ++k) {
                            if (strcmp(ifname, neigh_filter_names[k]) == 0) {
                                matched = 1;
                                break;
                            }
                        }
                        if (!matched) break;
                    }
                    parse_neighmsg(nlh);
                    break;
                }
            }

            // 多部分消息结束标记
            if (nlh->nlmsg_type == NLMSG_DONE)
                break;

            // 错误消息处理
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);
                if (err->error == 0) {
                    printf("  Acknowledgment message\n");
                } else {
                    printf("  ERROR: %d - %s\n",
                           -err->error, strerror(-err->error));
                }
            }
        }
    }
    if (record_to_file && saved >= 0) {
        printf("Write done to file %s...\n", file_to_write);
        restore_stdout(saved);
    }
    close(nl_sock);
    return 0;
}