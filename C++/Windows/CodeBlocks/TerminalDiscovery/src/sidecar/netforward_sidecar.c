#include "netforward_sidecar.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int g_listen_fd = -1;
static int g_conn_fd = -1;
static char g_socket_path[108];

static int write_full(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int setup_listener(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int accept_client(void) {
    if (g_listen_fd < 0) {
        errno = ENOTCONN;
        return -1;
    }

    for (;;) {
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd >= 0) {
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

int netforward_sidecar_start(const char *socket_path) {
    const char *path = socket_path && socket_path[0] ? socket_path : TD_NETFORWARD_DEFAULT_SOCKET;

    if (g_conn_fd != -1 || g_listen_fd != -1) {
        netforward_sidecar_stop();
    }

    memset(g_socket_path, 0, sizeof(g_socket_path));
    snprintf(g_socket_path, sizeof(g_socket_path), "%s", path);

    g_listen_fd = setup_listener(g_socket_path);
    if (g_listen_fd < 0) {
        netforward_sidecar_stop();
        return -1;
    }

    g_conn_fd = accept_client();
    if (g_conn_fd < 0) {
        netforward_sidecar_stop();
        return -1;
    }

    return 0;
}

int netforward_sidecar_forward(unsigned char *buf, int len) {
    if (!buf || len <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (g_conn_fd < 0) {
        g_conn_fd = accept_client();
        if (g_conn_fd < 0) {
            return -1;
        }
    }

    if (write_full(g_conn_fd, buf, (size_t)len) == 0) {
        return 0;
    }

    close(g_conn_fd);
    g_conn_fd = accept_client();
    if (g_conn_fd < 0) {
        return -1;
    }

    return write_full(g_conn_fd, buf, (size_t)len);
}

void netforward_sidecar_stop(void) {
    if (g_conn_fd != -1) {
        close(g_conn_fd);
        g_conn_fd = -1;
    }
    if (g_listen_fd != -1) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_socket_path[0] != '\0') {
        unlink(g_socket_path);
        g_socket_path[0] = '\0';
    }
}
