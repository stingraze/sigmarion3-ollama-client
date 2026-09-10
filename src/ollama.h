#ifndef OLLAMA_CE_OLLAMA_H
#define OLLAMA_CE_OLLAMA_H

typedef int (*ollama_token_cb)(const char *text, int len, void *user);

/* Adds a missing http:// scheme and the default :11434 port, in place. */
void ollama_normalize_url(char *url, int cap);

int ollama_list_models(const char *base_url, char *out_names, int cap,
                       volatile int *cancel, char *err, int err_cap);

int ollama_generate(const char *base_url, const char *model, const char *prompt,
                    ollama_token_cb on_token, void *user,
                    volatile int *cancel, char *err, int err_cap);

#endif
