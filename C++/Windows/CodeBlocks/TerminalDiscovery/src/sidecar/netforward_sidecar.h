#ifndef NETFORWARD_SIDECAR_H
#define NETFORWARD_SIDECAR_H

#include <stddef.h>

#define TD_NETFORWARD_DEFAULT_SOCKET "/tmp/netforward_sidecar.sock"

int netforward_sidecar_start(const char *socket_path);
void netforward_sidecar_stop(void);
int netforward_sidecar_forward(unsigned char *buf, int len);

#endif /* NETFORWARD_SIDECAR_H */
