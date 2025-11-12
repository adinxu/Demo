#define _GNU_SOURCE

#include "terminal_netlink.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <pthread.h>
#include "td_atomic.h"
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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
    struct terminal_netlink_sync_state *sync_state;
};

struct terminal_netlink_sync_state {
    struct terminal_manager *manager;
    bool last_attempt_failed;
    bool logged_initial_success;
};

static void handle_netlink_message(struct terminal_netlink_listener *listener,
                                   struct nlmsghdr *nlh);

static uint32_t next_netlink_seq(void) {
    static uint32_t seq = 0U;
    return __sync_add_and_fetch(&seq, 1U);
}

static uint8_t prefix_len_from_mask(struct in_addr mask) {
    uint32_t host = ntohl(mask.s_addr);
    uint8_t prefix_len = 0U;
    bool gap = false;
    for (int bit = 31; bit >= 0; --bit) {
        bool is_one = (host & (1U << bit)) != 0U;
        if (is_one) {
            if (gap) {
                break;
            }
            prefix_len += 1U;
        } else {
            gap = true;
        }
    }
    return prefix_len;
}

static void publish_address_update(struct terminal_manager *manager,
                                   int ifindex,
                                   struct in_addr address,
                                   uint8_t prefix_len) {
    if (!manager || ifindex <= 0) {
        return;
    }

    terminal_address_update_t update = {
        .kernel_ifindex = ifindex,
        .address = address,
        .prefix_len = prefix_len,
        .is_add = true,
    };
    terminal_manager_on_address_update(manager, &update);
}

static int sync_addresses_via_getifaddrs(struct terminal_manager *manager) {
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) {
        return -errno;
    }

    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        int ifindex = (int)if_nametoindex(ifa->ifa_name);
        if (ifindex <= 0) {
            continue;
        }

        struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
        struct in_addr address = addr_in->sin_addr;
        uint8_t prefix_len = 32U;

        if (ifa->ifa_netmask && ifa->ifa_netmask->sa_family == AF_INET) {
            struct sockaddr_in *mask_in = (struct sockaddr_in *)ifa->ifa_netmask;
            prefix_len = prefix_len_from_mask(mask_in->sin_addr);
        }

        publish_address_update(manager, ifindex, address, prefix_len);
    }

    freeifaddrs(ifaddr);
    return 0;
}

static int sync_addresses_via_netlink(struct terminal_manager *manager) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        return -errno;
    }

    struct sockaddr_nl local;
    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;

    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }

    struct {
        struct nlmsghdr hdr;
        struct rtgenmsg gen;
    } req;

    memset(&req, 0, sizeof(req));
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
    req.hdr.nlmsg_type = RTM_GETADDR;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
    req.hdr.nlmsg_seq = next_netlink_seq();
    req.hdr.nlmsg_pid = (uint32_t)getpid();
    req.gen.rtgen_family = AF_INET;

    struct sockaddr_nl kernel;
    memset(&kernel, 0, sizeof(kernel));
    kernel.nl_family = AF_NETLINK;

    if (sendto(fd, &req, req.hdr.nlmsg_len, 0, (struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
        int err = -errno;
        close(fd);
        return err;
    }

    uint8_t buffer[TD_NETLINK_BUFFER_SIZE];
    bool done = false;
    int rc = 0;

    struct terminal_netlink_listener temp_listener;
    memset(&temp_listener, 0, sizeof(temp_listener));
    temp_listener.fd = fd;
    temp_listener.manager = manager;

    while (!done) {
        ssize_t len = recv(fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            rc = -errno;
            break;
        }

        if (len == 0) {
            rc = -EIO;
            break;
        }

        int msg_len = (int)len;
        for (struct nlmsghdr *nlh = (struct nlmsghdr *)buffer; NLMSG_OK(nlh, msg_len);
             nlh = NLMSG_NEXT(nlh, msg_len)) {
            if (nlh->nlmsg_seq != req.hdr.nlmsg_seq) {
                continue;
            }

            if (nlh->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);
                if (err && err->error == 0) {
                    done = true;
                    rc = 0;
                } else {
                    rc = err ? err->error : -EIO;
                    if (rc >= 0) {
                        rc = -EIO;
                    }
                }
                done = true;
                break;
            }

            handle_netlink_message(&temp_listener, nlh);
        }
    }

    close(fd);
    return rc;
}

static int terminal_netlink_sync_address_table(struct terminal_manager *manager,
                                               bool *used_fallback) {
    if (used_fallback) {
        *used_fallback = false;
    }

    int rc = sync_addresses_via_netlink(manager);
    if (rc == 0) {
        return 0;
    }

    int fallback_rc = sync_addresses_via_getifaddrs(manager);
    if (fallback_rc == 0) {
        if (used_fallback) {
            *used_fallback = true;
        }
        return 0;
    }

    if (fallback_rc != 0) {
        return fallback_rc;
    }
    return rc;
}

static int terminal_netlink_sync_handler(void *ctx) {
    struct terminal_netlink_sync_state *state = (struct terminal_netlink_sync_state *)ctx;
    if (!state || !state->manager) {
        return -EINVAL;
    }

    bool used_fallback = false;
    int rc = terminal_netlink_sync_address_table(state->manager, &used_fallback);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        if (!state->last_attempt_failed) {
            td_log_writef(TD_LOG_WARN,
                          "netlink_listener",
                          "address table sync failed: %s",
                          strerror(err));
        }
        state->last_attempt_failed = true;
        return rc;
    }

    if (used_fallback) {
        td_log_writef(TD_LOG_WARN,
                      "netlink_listener",
                      "netlink dump unavailable; populated IPv4 address table via getifaddrs()");
    } else if (!state->logged_initial_success) {
        td_log_writef(TD_LOG_INFO,
                      "netlink_listener",
                      "initial IPv4 address table sync completed");
    } else if (state->last_attempt_failed) {
        td_log_writef(TD_LOG_INFO,
                      "netlink_listener",
                      "address table sync recovered");
    }

    state->last_attempt_failed = false;
    state->logged_initial_success = true;
    return 0;
}

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
        .kernel_ifindex = (int)ifa->ifa_index,
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

    struct terminal_netlink_sync_state *sync_state = calloc(1, sizeof(*sync_state));
    if (!sync_state) {
        td_log_writef(TD_LOG_ERROR, "netlink_listener", "failed to allocate sync state");
        close(listener->fd);
        listener->fd = -1;
        free(listener);
        return -1;
    }

    sync_state->manager = manager;
    listener->sync_state = sync_state;

    terminal_manager_set_address_sync_handler(manager, terminal_netlink_sync_handler, sync_state);

    int initial_rc = terminal_netlink_sync_handler(sync_state);
    if (initial_rc != 0) {
        terminal_manager_request_address_sync(manager);
    }

    int rc = pthread_create(&listener->thread, NULL, netlink_thread_main, listener);
    if (rc != 0) {
        td_log_writef(TD_LOG_ERROR, "netlink_listener", "pthread_create failed: %s", strerror(rc));
        atomic_store(&listener->running, false);
        close(listener->fd);
        listener->fd = -1;
        terminal_manager_set_address_sync_handler(manager, NULL, NULL);
        free(sync_state);
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

    if (listener->manager) {
        terminal_manager_set_address_sync_handler(listener->manager, NULL, NULL);
    }

    if (listener->sync_state) {
        free(listener->sync_state);
        listener->sync_state = NULL;
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
