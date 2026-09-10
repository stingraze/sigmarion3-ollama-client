#ifndef OLLAMA_CE_CONFIG_H
#define OLLAMA_CE_CONFIG_H

#include "compat.h"

typedef struct {
    char url[OLLAMA_CE_MAX_URL];
    char model[OLLAMA_CE_MAX_MODEL];
} app_config_t;

void config_default(app_config_t *c);
int config_load(app_config_t *c);
int config_save(const app_config_t *c);

#endif
