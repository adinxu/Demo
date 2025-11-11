#ifndef TERMINAL_DISCOVERY_EMBED_H
#define TERMINAL_DISCOVERY_EMBED_H

#include "td_config.h"
#include "terminal_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

struct app_context;

struct terminal_discovery_init_params {
    const struct td_runtime_config *runtime_config; /* optional overrides; NULL to use defaults */
};

int terminal_discovery_initialize(const struct terminal_discovery_init_params *params);

struct terminal_manager *terminal_discovery_get_manager(void);

const struct app_context *terminal_discovery_get_app_context(void);

#ifdef __cplusplus
}
#endif

#endif /* TERMINAL_DISCOVERY_EMBED_H */
