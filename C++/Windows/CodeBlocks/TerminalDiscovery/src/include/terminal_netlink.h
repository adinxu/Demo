#ifndef TERMINAL_NETLINK_H
#define TERMINAL_NETLINK_H

#include <stdbool.h>

struct terminal_manager;
struct terminal_netlink_listener;

int terminal_netlink_start(struct terminal_manager *manager,
                           struct terminal_netlink_listener **listener_out);

void terminal_netlink_stop(struct terminal_netlink_listener *listener);

#endif /* TERMINAL_NETLINK_H */
