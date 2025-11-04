#define _GNU_SOURCE

#include "terminal_netlink.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "td_logging.h"
#include "terminal_manager.h"

#ifndef TD_NETLINK_BUFFER_SIZE
#define TD_NETLINK_BUFFER_SIZE 8192
#endif

struct terminal_netlink_listener {
    int fd;
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
    struct terminal_manager *manager;
};

static void handle_netlink_message(struct terminal_netlink_listener *listener,
                                   struct nlmsghdr *nlh) {
    if (!listener || !nlh) {
        return;
    }

    if (nlh->nlmsg_type != RTM_NEWADDR && nlh->nlmsg_type != RTM_DELADDR) {
        return;
    }

    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    if (!ifa || ifa->ifa_family != AF_INET) {
        return;
    }

    struct in_addr addr;
    memset(&addr, 0, sizeof(addr));
    bool have_addr = false;

    int attr_len = (int)nlh->nlmsg_len - (int)NLMSG_LENGTH(sizeof(*ifa));
    if (attr_len < 0) {
        attr_len = 0;
    }
    for (struct rtattr *attr = IFA_RTA(ifa); attr && RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
        if (attr->rta_type == IFA_LOCAL || attr->rta_type == IFA_ADDRESS) {
            if (RTA_PAYLOAD(attr) >= sizeof(addr)) {
                memcpy(&addr, RTA_DATA(attr), sizeof(addr));
                have_addr = true;
                break;
            }
        }
    }

    if (!have_addr) {
        return;
    }

    uint8_t prefix_len = ifa->ifa_prefixlen;
    if (prefix_len > 32U) {
        prefix_len = 32U;
    }

    terminal_address_update_t update = {
        .ifindex = (int)ifa->ifa_index,
        .address = addr,
        .prefix_len = prefix_len,
        .is_add = (nlh->nlmsg_type == RTM_NEWADDR),
    };

    if (listener->manager) {
        terminal_manager_on_address_update(listener->manager, &update);
    }
}

static void *netlink_thread_main(void *arg) {
    struct terminal_netlink_listener *listener = (struct terminal_netlink_listener *)arg;
    td_log_writef(TD_LOG_INFO, "netlink_listener", "thread started");

    uint8_t buffer[TD_NETLINK_BUFFER_SIZE];

    while (atomic_load(&listener->running)) {
        struct pollfd pfd = {
            .fd = listener->fd,
            .events = POLLIN,
            .revents = 0,
        };

        int rc = poll(&pfd, 1, 1000);
        if (!atomic_load(&listener->running)) {
            break;
        }

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            td_log_writef(TD_LOG_ERROR, "netlink_listener", "poll error: %s", strerror(errno));
            break;
        }

        if (rc == 0) {
            continue;
        }

        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        ssize_t len = recv(listener->fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            if (!atomic_load(&listener->running) && errno == EBADF) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            td_log_writef(TD_LOG_ERROR, "netlink_listener", "recv error: %s", strerror(errno));
            break;
        }

        int msg_len = (int)len;
        if (msg_len <= 0) {
            continue;
        }

        for (struct nlmsghdr *nlh = (struct nlmsghdr *)buffer; NLMSG_OK(nlh, msg_len);
             nlh = NLMSG_NEXT(nlh, msg_len)) {
            if (nlh->nlmsg_type == NLMSG_ERROR || nlh->nlmsg_type == NLMSG_NOOP) {
                continue;
            }
            handle_netlink_message(listener, nlh);
        }
    }

    td_log_writef(TD_LOG_INFO, "netlink_listener", "thread exiting");
    return NULL;
}

int terminal_netlink_start(struct terminal_manager *manager,
                           struct terminal_netlink_listener **listener_out) {
    if (!manager || !listener_out) {
        return -1;
    }

    struct terminal_netlink_listener *listener = calloc(1, sizeof(*listener));
    if (!listener) {
        return -1;
    }

    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        td_log_writef(TD_LOG_ERROR, "netlink_listener", "socket(AF_NETLINK) failed: %s", strerror(errno));
        free(listener);
        return -1;
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_IPV4_IFADDR;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        td_log_writef(TD_LOG_ERROR, "netlink_listener", "bind failed: %s", strerror(errno));
        close(fd);
        free(listener);
        return -1;
    }

    listener->fd = fd;
    listener->manager = manager;
    listener->thread_started = false;
    atomic_init(&listener->running, true);

    int rc = pthread_create(&listener->thread, NULL, netlink_thread_main, listener);
    if (rc != 0) {
        td_log_writef(TD_LOG_ERROR, "netlink_listener", "pthread_create failed: %s", strerror(rc));
        atomic_store(&listener->running, false);
        close(listener->fd);
        listener->fd = -1;
        free(listener);
        return -1;
    }

    listener->thread_started = true;
    td_log_writef(TD_LOG_INFO, "netlink_listener", "listening for IPv4 address events");

    *listener_out = listener;
    return 0;
}

void terminal_netlink_stop(struct terminal_netlink_listener *listener) {
    if (!listener) {
        return;
    }

    atomic_store(&listener->running, false);

    if (listener->fd >= 0) {
        close(listener->fd);
        listener->fd = -1;
    }

    if (listener->thread_started) {
        pthread_join(listener->thread, NULL);
        listener->thread_started = false;
    }

    free(listener);
}
