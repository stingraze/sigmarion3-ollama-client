#ifndef OLLAMA_CE_JSONUTIL_H
#define OLLAMA_CE_JSONUTIL_H

int json_escape(const char *in, char *out, int out_cap);
int json_unescape(const char *in, int in_len, char *out, int out_cap);
int json_get_string(const char *json, const char *key, char *out, int out_cap);
int json_get_bool(const char *json, const char *key, int *out);
int json_collect_model_names(const char *json, char *out, int out_cap);

#endif
