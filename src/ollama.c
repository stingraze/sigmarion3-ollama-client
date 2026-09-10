#include "ollama.h"
#include "compat.h"
#include "http.h"
#include "jsonutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OLLAMA_DEFAULT_PORT "11434"

typedef struct {
    ollama_token_cb on_token;
    void *user;
    char err[256];
    int failed;
} gen_ctx_t;

void ollama_normalize_url(char *url, int cap)
{
    char work[OLLAMA_CE_MAX_URL * 2];
    char *host;
    char *slash;
    int len;

    if (!url || cap < 8) {
        return;
    }

    while (*url == ' ' || *url == '\t') {
        memmove(url, url + 1, strlen(url));
    }
    len = (int)strlen(url);
    while (len > 0 && (url[len - 1] == ' ' || url[len - 1] == '\t' ||
                       url[len - 1] == '\r' || url[len - 1] == '\n')) {
        url[--len] = 0;
    }
    if (len == 0) {
        strncpy(url, OLLAMA_CE_DEFAULT_URL, (size_t)cap - 1);
        url[cap - 1] = 0;
        return;
    }

    if (strstr(url, "://") == NULL) {
        if (len + 8 >= (int)sizeof(work)) {
            return;
        }
        strcpy(work, "http://");
        strcat(work, url);
    } else {
        if (len >= (int)sizeof(work)) {
            return;
        }
        strcpy(work, url);
    }

    /* Trailing slashes would turn into "//api/tags" once a path is appended. */
    len = (int)strlen(work);
    while (len > 0 && work[len - 1] == '/') {
        work[--len] = 0;
    }

    host = strstr(work, "://");
    host = host ? host + 3 : work;
    slash = strchr(host, '/');

    if (strchr(host, ':') == NULL) {
        char tail[OLLAMA_CE_MAX_URL];

        tail[0] = 0;
        if (slash) {
            strncpy(tail, slash, sizeof(tail) - 1);
            tail[sizeof(tail) - 1] = 0;
            *slash = 0;
        }
        if ((int)(strlen(work) + strlen(tail) + sizeof(OLLAMA_DEFAULT_PORT) + 1) < (int)sizeof(work)) {
            strcat(work, ":" OLLAMA_DEFAULT_PORT);
            strcat(work, tail);
        } else if (slash) {
            *slash = '/';
        }
    }

    if ((int)strlen(work) < cap) {
        strcpy(url, work);
    }
}

int ollama_list_models(const char *base_url, char *out_names, int cap,
                       volatile int *cancel, char *err, int err_cap)
{
    char *body;
    int len;
    int rc;
    char api_err[256];

    if (!out_names || cap < 1) {
        return -1;
    }
    out_names[0] = 0;
    body = NULL;
    len = 0;
    api_err[0] = 0;

    rc = http_get(base_url, "/api/tags", &body, &len, cancel, api_err, (int)sizeof(api_err));
    if (rc != 0) {
        if (err && err_cap > 0) {
            strncpy(err, api_err[0] ? api_err : "Failed to list models", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        http_free(body);
        return -1;
    }

    if (json_get_string(body, "error", api_err, (int)sizeof(api_err)) && api_err[0]) {
        if (err && err_cap > 0) {
            strncpy(err, api_err, (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        http_free(body);
        return -1;
    }

    rc = json_collect_model_names(body, out_names, cap);
    http_free(body);
    if (rc <= 0) {
        if (err && err_cap > 0) {
            strncpy(err, "No models reported by Ollama", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }
    return 0;
}

static int on_gen_line(const char *line, int len, void *user)
{
    gen_ctx_t *ctx;
    char *copy;
    char token[4096];
    char api_err[256];
    int done;

    ctx = (gen_ctx_t *)user;
    copy = (char *)malloc((size_t)len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, line, (size_t)len);
    copy[len] = 0;

    api_err[0] = 0;
    if (json_get_string(copy, "error", api_err, (int)sizeof(api_err)) && api_err[0]) {
        strncpy(ctx->err, api_err, sizeof(ctx->err) - 1);
        ctx->failed = 1;
        free(copy);
        return -1;
    }

    token[0] = 0;
    if (json_get_string(copy, "response", token, (int)sizeof(token)) && token[0]) {
        if (ctx->on_token) {
            ctx->on_token(token, (int)strlen(token), ctx->user);
        }
    }

    done = 0;
    json_get_bool(copy, "done", &done);
    free(copy);
    return 0;
}

int ollama_generate(const char *base_url, const char *model, const char *prompt,
                    ollama_token_cb on_token, void *user,
                    volatile int *cancel, char *err, int err_cap)
{
    char *escaped;
    char *body;
    char model_esc[256];
    int esc_cap;
    int body_cap;
    int n;
    int rc;
    gen_ctx_t ctx;
    char http_err[256];

    if (!model || !model[0]) {
        if (err && err_cap > 0) {
            strncpy(err, "Select or type a model name", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }
    if (!prompt) {
        prompt = "";
    }

    esc_cap = (int)strlen(prompt) * 6 + 8;
    if (esc_cap < 64) {
        esc_cap = 64;
    }
    escaped = (char *)malloc((size_t)esc_cap);
    body_cap = esc_cap + (int)strlen(model) + 64;
    body = (char *)malloc((size_t)body_cap);
    if (!escaped || !body) {
        free(escaped);
        free(body);
        if (err && err_cap > 0) {
            strncpy(err, "Out of memory", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }

    if (json_escape(prompt, escaped, esc_cap) < 0) {
        free(escaped);
        free(body);
        if (err && err_cap > 0) {
            strncpy(err, "Prompt too large", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }

    if (json_escape(model, model_esc, (int)sizeof(model_esc)) < 0) {
        free(escaped);
        free(body);
        if (err && err_cap > 0) {
            strncpy(err, "Model name too large", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }

    n = snprintf(body, (size_t)body_cap,
                 "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":true}",
                 model_esc, escaped);
    free(escaped);
    if (n <= 0 || n >= body_cap) {
        free(body);
        if (err && err_cap > 0) {
            strncpy(err, "Failed to build request", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.on_token = on_token;
    ctx.user = user;
    http_err[0] = 0;

    rc = http_post_lines(base_url, "/api/generate", body, n,
                         on_gen_line, &ctx, cancel, http_err, (int)sizeof(http_err));
    free(body);

    if (ctx.failed) {
        if (err && err_cap > 0) {
            strncpy(err, ctx.err, (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }
    if (rc != 0) {
        if (err && err_cap > 0) {
            strncpy(err, http_err[0] ? http_err : "Generate failed", (size_t)err_cap - 1);
            err[err_cap - 1] = 0;
        }
        return -1;
    }
    return 0;
}
