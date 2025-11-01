#include "td_adapter_registry.h"

#include <string.h>

#include "realtek_adapter.h"

static const struct td_adapter_descriptor *g_adapters[] = {
    NULL,
};

static void ensure_initialized(void) {
    if (!g_adapters[0]) {
        g_adapters[0] = td_realtek_adapter_descriptor();
    }
}

const struct td_adapter_descriptor *td_adapter_registry_find(const char *name) {
    ensure_initialized();
    if (!name) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(g_adapters) / sizeof(g_adapters[0]); ++i) {
        const struct td_adapter_descriptor *desc = g_adapters[i];
        if (desc && desc->name && strcmp(desc->name, name) == 0) {
            return desc;
        }
    }
    return NULL;
}

const struct td_adapter_descriptor *td_adapter_registry_get(size_t index) {
    ensure_initialized();
    if (index >= sizeof(g_adapters) / sizeof(g_adapters[0])) {
        return NULL;
    }
    return g_adapters[index];
}

size_t td_adapter_registry_count(void) {
    ensure_initialized();
    return sizeof(g_adapters) / sizeof(g_adapters[0]);
}
