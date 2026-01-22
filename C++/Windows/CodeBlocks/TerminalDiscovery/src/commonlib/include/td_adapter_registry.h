#ifndef TD_ADAPTER_REGISTRY_H
#define TD_ADAPTER_REGISTRY_H

#include <stddef.h>

#include "adapter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

const struct td_adapter_descriptor *td_adapter_registry_find(const char *name);

const struct td_adapter_descriptor *td_adapter_registry_get(size_t index);

size_t td_adapter_registry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* TD_ADAPTER_REGISTRY_H */
